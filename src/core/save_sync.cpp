// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "save_sync.h"
#include "host.h"
#include "save_sync_backends.h"
#include "settings.h"

#include "util/imgui_manager.h"

#include "common/error.h"
#include "common/file_system.h"
#include "common/log.h"
#include "common/path.h"
#include "common/string_util.h"

#include "IconsFontAwesome5.h"
#include "fmt/chrono.h"
#include "fmt/format.h"

#include <xxhash.h>

#include <array>
#include <ctime>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

#ifdef _WIN32
#include "common/windows_headers.h"
#else
#include <unistd.h>
#endif

Log_SetChannel(SaveSync);

namespace SaveSync {
namespace {

struct Category
{
  const char* remote_directory;
  const char* setting_key;
  const std::string& local_directory;
};

struct LocalFile
{
  s64 size;
  std::time_t modified;
};

/// What both sides held when a file was last synced. A side whose file differs from this has changed it since.
struct ManifestEntry
{
  u64 hash;
  s64 size;
  std::time_t modified;
  std::string tag;
};

using Manifest = std::map<std::string, ManifestEntry>;

} // namespace

static constexpr std::array s_mode_names = {"Disabled", "Folder", "WebDAV"};
static constexpr std::array s_mode_display_names = {TRANSLATE_NOOP("SaveSync", "Disabled"),
                                                    TRANSLATE_NOOP("SaveSync", "Folder"),
                                                    TRANSLATE_NOOP("SaveSync", "WebDAV Server")};
static_assert(s_mode_names.size() == static_cast<size_t>(Mode::Count));
static_assert(s_mode_display_names.size() == static_cast<size_t>(Mode::Count));

static constexpr const char* SETTINGS_SECTION = "SaveSync";
static constexpr const char* MANIFEST_IDENTITY_KEY = "target";
static constexpr s64 BACKUP_RETENTION_SECONDS = 30 * 24 * 60 * 60;

static const std::array<Category, 3> s_categories = {{
  {"memcards", "MemoryCards", EmuFolders::MemoryCards},
  {"savestates", "SaveStates", EmuFolders::SaveStates},
  {"gamesettings", "GameSettings", EmuFolders::GameSettings},
}};

static std::string GetSyncDataPath(std::string_view name)
{
  return Path::Combine(Path::Combine(EmuFolders::DataRoot, "savesync"), name);
}

static std::string GetDeviceName()
{
  std::string name = Host::GetBaseStringSettingValue(SETTINGS_SECTION, "DeviceName");
  if (!name.empty())
    return name;

#ifdef _WIN32
  wchar_t buffer[MAX_COMPUTERNAME_LENGTH + 1];
  DWORD length = static_cast<DWORD>(std::size(buffer));
  if (GetComputerNameW(buffer, &length))
    name = StringUtil::WideStringToUTF8String(std::wstring_view(buffer, length));
#else
  char buffer[256] = {};
  if (gethostname(buffer, sizeof(buffer) - 1) == 0)
    name = buffer;
#endif

  return name.empty() ? std::string("Riverhorse") : name;
}

static std::unique_ptr<Backend> CreateBackend(Mode mode, std::string* identity, Error* error)
{
  if (mode == Mode::Folder)
  {
    std::string folder = Host::GetBaseStringSettingValue(SETTINGS_SECTION, "Folder");
    if (folder.empty())
    {
      Error::SetStringView(error, TRANSLATE_SV("SaveSync", "No sync folder is set."));
      return nullptr;
    }

    if (!Path::IsAbsolute(folder))
      folder = Path::Combine(EmuFolders::DataRoot, folder);

    *identity = fmt::format("Folder {}", folder);
    return Backend::CreateFolder(std::move(folder));
  }

  std::string url = Host::GetBaseStringSettingValue(SETTINGS_SECTION, "URL");
  if (url.empty())
  {
    Error::SetStringView(error, TRANSLATE_SV("SaveSync", "No WebDAV server address is set."));
    return nullptr;
  }

  const std::string username = Host::GetBaseStringSettingValue(SETTINGS_SECTION, "Username");
  *identity = fmt::format("WebDAV {} {}", url, username);
  return Backend::CreateWebDAV(std::move(url), username, Host::GetBaseStringSettingValue(SETTINGS_SECTION, "Password"));
}

static Manifest LoadManifest(std::string_view identity)
{
  Manifest manifest;
  const std::optional<std::string> contents = FileSystem::ReadFileToString(GetSyncDataPath("manifest.txt").c_str());
  if (!contents.has_value())
    return manifest;

  const std::vector<std::string_view> lines = StringUtil::SplitString(contents.value(), '\n', true);
  if (lines.empty() || lines.front() != fmt::format("{}\t{}", MANIFEST_IDENTITY_KEY, identity))
    return manifest;

  for (size_t i = 1; i < lines.size(); i++)
  {
    const std::vector<std::string_view> fields = StringUtil::SplitString(lines[i], '\t', false);
    if (fields.size() < 4)
      continue;

    const std::optional<u64> hash = StringUtil::FromChars<u64>(fields[1], 16);
    const std::optional<s64> size = StringUtil::FromChars<s64>(fields[2]);
    const std::optional<s64> modified = StringUtil::FromChars<s64>(fields[3]);
    if (hash.has_value() && size.has_value() && modified.has_value())
    {
      manifest.emplace(std::string(fields[0]),
                       ManifestEntry{hash.value(), size.value(), static_cast<std::time_t>(modified.value()),
                                     std::string((fields.size() > 4) ? fields[4] : std::string_view())});
    }
  }

  return manifest;
}

static bool SaveManifest(std::string_view identity, const Manifest& manifest, Error* error)
{
  std::string contents = fmt::format("{}\t{}\n", MANIFEST_IDENTITY_KEY, identity);
  for (const auto& [key, entry] : manifest)
  {
    contents += fmt::format("{}\t{:016x}\t{}\t{}\t{}\n", key, entry.hash, entry.size, static_cast<s64>(entry.modified),
                            entry.tag);
  }

  const std::string path = GetSyncDataPath("manifest.txt");
  return FileSystem::EnsureDirectoryExists(std::string(Path::GetDirectory(path)).c_str(), true, error) &&
         FileSystem::WriteAtomicRenamedFile(path, contents.data(), contents.size(), error);
}

static u64 HashData(std::span<const u8> data)
{
  return XXH64(data.data(), data.size(), 0);
}

static std::string MakeConflictName(std::string_view name, std::string_view who, std::time_t when)
{
  const size_t dot = name.rfind('.');
  const std::string_view stem = (dot == std::string_view::npos) ? name : name.substr(0, dot);
  const std::string_view extension = (dot == std::string_view::npos) ? std::string_view() : name.substr(dot);
  return fmt::format("{} (conflict {} {:%Y-%m-%d %H-%M}){}", stem, who, fmt::localtime(when), extension);
}

/// Copies a local file about to be replaced or deleted into the backup folder.
static void BackupLocalFile(const Category& category, std::string_view name)
{
  const std::string source = Path::Combine(category.local_directory, name);
  const std::string directory = GetSyncDataPath(Path::Combine("backup", category.remote_directory));
  const std::string destination =
    Path::Combine(directory, fmt::format("{}.{:%Y%m%d-%H%M%S}", name, fmt::localtime(std::time(nullptr))));

  Error error;
  std::optional<std::vector<u8>> data = FileSystem::ReadBinaryFile(source.c_str(), &error);
  if (!data.has_value() || !FileSystem::EnsureDirectoryExists(directory.c_str(), true, &error) ||
      !FileSystem::WriteBinaryFile(destination.c_str(), data->data(), data->size(), &error))
  {
    WARNING_LOG("Couldn't back up '{}': {}", source, error.GetDescription());
  }
}

static void PruneBackups()
{
  const std::string directory = GetSyncDataPath("backup");
  FileSystem::FindResultsArray results;
  FileSystem::FindFiles(directory.c_str(), "*", FILESYSTEM_FIND_FILES | FILESYSTEM_FIND_RECURSIVE, &results);

  const std::time_t now = std::time(nullptr);
  for (const FILESYSTEM_FIND_DATA& result : results)
  {
    if (static_cast<s64>(now - result.ModificationTime) > BACKUP_RETENTION_SECONDS)
      FileSystem::DeleteFile(result.FileName.c_str());
  }
}

/// A timestamp from the last seconds is stored as 0, so the file is hashed again next time instead of trusted.
static std::time_t TrustedModified(std::time_t modified)
{
  return IsTimestampTooRecent(modified) ? 0 : modified;
}

static bool WriteLocalFile(const Category& category, std::string_view name, std::span<const u8> data,
                           ManifestEntry* entry, Error* error)
{
  const std::string path = Path::Combine(category.local_directory, name);
  if (FileSystem::FileExists(path.c_str()))
    BackupLocalFile(category, name);

  FILESYSTEM_STAT_DATA stat;
  if (!FileSystem::EnsureDirectoryExists(category.local_directory.c_str(), true, error) ||
      !FileSystem::WriteAtomicRenamedFile(path, data.data(), data.size(), error) ||
      !FileSystem::StatFile(path.c_str(), &stat))
  {
    return false;
  }

  entry->hash = HashData(data);
  entry->size = stat.Size;
  entry->modified = TrustedModified(stat.ModificationTime);
  return true;
}

static bool SyncCategory(Backend& backend, const Category& category, std::string_view device_name, Manifest& manifest,
                         Result* result, Error* error)
{
  std::map<std::string, LocalFile, std::less<>> local_files;
  FileSystem::FindResultsArray results;
  FileSystem::FindFiles(category.local_directory.c_str(), "*", FILESYSTEM_FIND_FILES | FILESYSTEM_FIND_RELATIVE_PATHS,
                        &results);
  for (const FILESYSTEM_FIND_DATA& file : results)
    local_files.emplace(file.FileName, LocalFile{file.Size, file.ModificationTime});

  RemoteListing remote_files;
  if (!backend.ListFiles(category.remote_directory, &remote_files, error))
    return false;

  std::set<std::string, std::less<>> names;
  for (const auto& [name, file] : local_files)
    names.insert(name);
  for (const auto& [name, file] : remote_files)
    names.insert(name);

  std::vector<std::string> uploaded_keys;
  const auto upload = [&](std::string_view name, std::span<const u8> data, ManifestEntry entry) {
    INFO_LOG("Sending {}/{}", category.remote_directory, name);
    if (!backend.WriteFile(category.remote_directory, name, data, error))
      return false;

    const std::string key = Path::Combine(category.remote_directory, name);
    manifest[key] = std::move(entry);
    uploaded_keys.push_back(key);
    result->uploaded++;
    return true;
  };

  for (const std::string& name : names)
  {
    const std::string key = Path::Combine(category.remote_directory, name);
    const std::string local_path = Path::Combine(category.local_directory, name);
    const auto base = manifest.find(key);
    const bool has_base = (base != manifest.end());
    const auto local = local_files.find(name);
    const auto remote = remote_files.find(name);
    const bool has_local = (local != local_files.end());
    const bool has_remote = (remote != remote_files.end());

    std::optional<std::vector<u8>> local_data;
    u64 local_hash = 0;
    if (has_local)
    {
      if (has_base && base->second.size == local->second.size && base->second.modified == local->second.modified)
      {
        local_hash = base->second.hash;
      }
      else
      {
        local_data = FileSystem::ReadBinaryFile(local_path.c_str(), error);
        if (!local_data.has_value())
          return false;
        local_hash = HashData(local_data.value());
      }
    }

    const auto read_local_data = [&]() {
      if (!local_data.has_value())
        local_data = FileSystem::ReadBinaryFile(local_path.c_str(), error);
      return local_data.has_value();
    };
    const ManifestEntry local_entry =
      has_local ?
        ManifestEntry{local_hash, local->second.size, TrustedModified(local->second.modified), std::string()} :
        ManifestEntry{};

    const bool local_changed = has_base ? (!has_local || local_hash != base->second.hash) : has_local;
    const bool remote_changed =
      has_base ? (!has_remote || remote->second.tag.empty() || remote->second.tag != base->second.tag) : has_remote;

    if (!local_changed && !remote_changed)
    {
      if (has_local)
      {
        ManifestEntry entry = local_entry;
        entry.tag = remote->second.tag;
        manifest[key] = std::move(entry);
      }
      continue;
    }

    if (local_changed && !(remote_changed && has_remote))
    {
      if (has_local)
      {
        if (!read_local_data() || !upload(name, local_data.value(), local_entry))
          return false;
      }
      else
      {
        if (has_remote)
        {
          INFO_LOG("Removing {}/{} from the remote", category.remote_directory, name);
          if (!backend.DeleteFile(category.remote_directory, name, error))
            return false;
          result->deleted++;
        }
        manifest.erase(key);
      }
      continue;
    }

    if (!has_remote)
    {
      if (has_local)
      {
        INFO_LOG("Removing {}/{} here", category.remote_directory, name);
        BackupLocalFile(category, name);
        if (!FileSystem::DeleteFile(local_path.c_str(), error))
          return false;
        result->deleted++;
      }
      manifest.erase(key);
      continue;
    }

    std::optional<std::vector<u8>> remote_data = backend.ReadFile(category.remote_directory, name, error);
    if (!remote_data.has_value())
      return false;

    const u64 remote_hash = HashData(remote_data.value());
    if (has_base && remote_hash == base->second.hash && local_changed)
    {
      if (has_local)
      {
        if (!read_local_data() || !upload(name, local_data.value(), local_entry))
          return false;
      }
      else
      {
        INFO_LOG("Removing {}/{} from the remote", category.remote_directory, name);
        if (!backend.DeleteFile(category.remote_directory, name, error))
          return false;
        manifest.erase(key);
        result->deleted++;
      }
      continue;
    }

    ManifestEntry entry;
    if (has_local && remote_hash == local_hash)
    {
      entry = local_entry;
      entry.tag = remote->second.tag;
      manifest[key] = std::move(entry);
      continue;
    }

    if (!local_changed || !has_local)
    {
      INFO_LOG("Receiving {}/{}", category.remote_directory, name);
      if (!WriteLocalFile(category, name, remote_data.value(), &entry, error))
        return false;

      entry.tag = remote->second.tag;
      manifest[key] = std::move(entry);
      result->downloaded++;
      continue;
    }

    if (!read_local_data())
      return false;

    result->conflicts++;
    if (remote->second.modified > local->second.modified)
    {
      const std::string conflict_name = MakeConflictName(name, device_name, local->second.modified);
      WARNING_LOG("'{}' changed here and on the remote; the remote copy is newer, this device's is kept as '{}'.", name,
                  conflict_name);

      ManifestEntry conflict_entry;
      if (!WriteLocalFile(category, conflict_name, local_data.value(), &conflict_entry, error) ||
          !upload(conflict_name, local_data.value(), std::move(conflict_entry)) ||
          !WriteLocalFile(category, name, remote_data.value(), &entry, error))
      {
        return false;
      }

      entry.tag = remote->second.tag;
      manifest[key] = std::move(entry);
      result->downloaded++;
    }
    else
    {
      const std::string conflict_name =
        MakeConflictName(name, TRANSLATE_SV("SaveSync", "other device"), remote->second.modified);
      WARNING_LOG("'{}' changed here and on the remote; this device's copy is newer, the remote one is kept as '{}'.",
                  name, conflict_name);

      ManifestEntry conflict_entry;
      if (!WriteLocalFile(category, conflict_name, remote_data.value(), &conflict_entry, error) ||
          !upload(conflict_name, remote_data.value(), std::move(conflict_entry)) ||
          !upload(name, local_data.value(), local_entry))
      {
        return false;
      }
    }
  }

  if (uploaded_keys.empty())
    return true;

  if (!backend.ListFiles(category.remote_directory, &remote_files, error))
    return false;

  for (const std::string& key : uploaded_keys)
  {
    const auto remote = remote_files.find(Path::GetFileName(key));
    if (remote != remote_files.end())
      manifest[key].tag = remote->second.tag;
  }

  return true;
}

std::optional<Mode> ParseModeName(const char* name)
{
  for (size_t i = 0; i < s_mode_names.size(); i++)
  {
    if (StringUtil::Strcasecmp(name, s_mode_names[i]) == 0)
      return static_cast<Mode>(i);
  }

  return std::nullopt;
}

const char* GetModeName(Mode mode)
{
  return s_mode_names[static_cast<size_t>(mode)];
}

const char* GetModeDisplayName(Mode mode)
{
  return Host::TranslateToCString("SaveSync", s_mode_display_names[static_cast<size_t>(mode)]);
}

bool IsEnabled()
{
  return ParseModeName(Host::GetBaseStringSettingValue(SETTINGS_SECTION, "Mode", GetModeName(Mode::Disabled)).c_str())
           .value_or(Mode::Disabled) != Mode::Disabled;
}

bool SyncNow(Result* result, Error* error)
{
  const Mode mode =
    ParseModeName(Host::GetBaseStringSettingValue(SETTINGS_SECTION, "Mode", GetModeName(Mode::Disabled)).c_str())
      .value_or(Mode::Disabled);
  if (mode == Mode::Disabled)
  {
    Error::SetStringView(error, TRANSLATE_SV("SaveSync", "Save sync is off."));
    return false;
  }

  std::string identity;
  std::unique_ptr<Backend> backend = CreateBackend(mode, &identity, error);
  if (!backend)
    return false;

  const std::string device_name = GetDeviceName();
  Manifest manifest = LoadManifest(identity);
  bool success = true;
  for (const Category& category : s_categories)
  {
    if (!Host::GetBaseBoolSettingValue(SETTINGS_SECTION, category.setting_key, true))
      continue;

    if (!SyncCategory(*backend, category, device_name, manifest, result, error))
    {
      success = false;
      break;
    }
  }

  Error manifest_error;
  if (!SaveManifest(identity, manifest, &manifest_error))
    ERROR_LOG("Couldn't save the sync manifest: {}", manifest_error.GetDescription());

  PruneBackups();
  INFO_LOG("Sync {}: {} uploaded, {} downloaded, {} deleted, {} conflicts", success ? "finished" : "stopped",
           result->uploaded, result->downloaded, result->deleted, result->conflicts);
  return success;
}

void SyncAndReport()
{
  if (!IsEnabled())
    return;

  Result result;
  Error error;
  if (!SyncNow(&result, &error))
  {
    Host::AddIconOSDMessage("SaveSync", ICON_FA_SYNC,
                            fmt::format(TRANSLATE_FS("SaveSync", "Save sync failed: {}"), error.GetDescription()),
                            Host::OSD_ERROR_DURATION);
    return;
  }

  if (result.conflicts > 0)
  {
    Host::AddIconOSDMessage(
      "SaveSync", ICON_FA_SYNC,
      TRANSLATE_PLURAL_STR("SaveSync",
                           "%n save(s) changed on two devices. Both versions are kept; the older one has "
                           "\"conflict\" in its name.",
                           "", result.conflicts),
      Host::OSD_WARNING_DURATION);
  }
  else if (result.uploaded > 0 || result.downloaded > 0 || result.deleted > 0)
  {
    Host::AddIconOSDMessage("SaveSync", ICON_FA_SYNC,
                            fmt::format(TRANSLATE_FS("SaveSync", "Saves synced: {} sent, {} received, {} removed."),
                                        result.uploaded, result.downloaded, result.deleted),
                            Host::OSD_INFO_DURATION);
  }
}

} // namespace SaveSync
