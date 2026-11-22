// SPDX-FileCopyrightText: 2019-2023 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only

#include "cd_image.h"
#include "cd_subchannel_replacement.h"

#include "common/assert.h"
#include "common/error.h"
#include "common/file_system.h"
#include "common/log.h"
#include "common/path.h"
#include "common/string_util.h"

#include <algorithm>
#include <cerrno>
#include <map>

Log_SetChannel(CDImageMemory);

namespace {

class CDImageM3u : public CDImage
{
public:
  CDImageM3u();
  ~CDImageM3u() override;

  bool Open(const char* path, bool apply_patches, Error* Error);

  bool ReadSubChannelQ(SubChannelQ* subq, const Index& index, LBA lba_in_index) override;
  bool HasNonStandardSubchannel() const override;
  bool LoadSubchannelReplacementFile(std::string_view path) override;

  bool HasSubImages() const override;
  u32 GetSubImageCount() const override;
  u32 GetCurrentSubImage() const override;
  std::string GetSubImageMetadata(u32 index, std::string_view type) const override;
  bool SwitchSubImage(u32 index, Error* error) override;

protected:
  bool ReadSectorFromIndex(void* buffer, const Index& index, LBA lba_in_index) override;

private:
  struct Entry
  {
    // TODO: Worth storing any other data?
    std::string filename;
    std::string title;
  };

  std::vector<Entry> m_entries;
  std::unique_ptr<CDImage> m_current_image;
  u32 m_current_image_index = UINT32_C(0xFFFFFFFF);
  bool m_apply_patches = false;
};

} // namespace

CDImageM3u::CDImageM3u() = default;

CDImageM3u::~CDImageM3u() = default;

std::vector<std::string> CDImage::ParseM3uFilenames(std::string_view m3u_path, std::string_view m3u_contents)
{
  std::vector<std::string> filenames;
  for (std::string_view line : StringUtil::SplitString(m3u_contents, '\n'))
  {
    line = StringUtil::StripWhitespace(line);
    if (line.empty() || line[0] == '#')
      continue;

    std::string filename = Path::ToNativePath(line);
    if (!Path::IsAbsolute(filename))
      filename = Path::BuildRelativePath(m3u_path, filename);
    filenames.push_back(std::move(filename));
  }

  return filenames;
}

std::vector<std::string> CDImage::ReadM3uFilenames(const char* m3u_path)
{
  const std::optional<std::string> m3u_file = FileSystem::ReadFileToString(m3u_path);
  return m3u_file.has_value() ? ParseM3uFilenames(m3u_path, m3u_file.value()) : std::vector<std::string>();
}

bool CDImageM3u::Open(const char* path, bool apply_patches, Error* error)
{
  std::FILE* fp = FileSystem::OpenSharedCFile(path, "rb", FileSystem::FileShareMode::DenyWrite, error);
  if (!fp)
    return false;

  std::optional<std::string> m3u_file(FileSystem::ReadFileToString(fp));
  std::fclose(fp);
  if (!m3u_file.has_value() || m3u_file->empty())
  {
    Error::SetString(error, "Failed to read M3u file");
    return false;
  }

  m_filename = path;
  m_apply_patches = apply_patches;

  for (std::string& entry_filename : ParseM3uFilenames(path, m3u_file.value()))
  {
    Entry entry;
    entry.title = Path::GetFileTitle(entry_filename);
    entry.filename = std::move(entry_filename);
    DEV_LOG("Read path from m3u: '{}'", entry.filename);
    m_entries.push_back(std::move(entry));
  }

  INFO_LOG("Loaded {} paths from m3u '{}'", m_entries.size(), path);
  return !m_entries.empty() && SwitchSubImage(0, error);
}

bool CDImageM3u::LoadSubchannelReplacementFile(std::string_view path)
{
  return m_current_image->LoadSubchannelReplacementFile(path);
}

bool CDImageM3u::HasNonStandardSubchannel() const
{
  return m_current_image->HasNonStandardSubchannel();
}

bool CDImageM3u::HasSubImages() const
{
  return true;
}

u32 CDImageM3u::GetSubImageCount() const
{
  return static_cast<u32>(m_entries.size());
}

u32 CDImageM3u::GetCurrentSubImage() const
{
  return m_current_image_index;
}

bool CDImageM3u::SwitchSubImage(u32 index, Error* error)
{
  if (index >= m_entries.size())
    return false;
  else if (index == m_current_image_index)
    return true;

  const Entry& entry = m_entries[index];
  std::unique_ptr<CDImage> new_image = CDImage::Open(entry.filename.c_str(), m_apply_patches, error);
  if (!new_image)
  {
    ERROR_LOG("Failed to load subimage {} ({})", index, entry.filename);
    return false;
  }

  CopyTOC(new_image.get());
  m_current_image = std::move(new_image);
  m_current_image_index = index;
  if (!Seek(1, Position{0, 0, 0}))
    Panic("Failed to seek to start after sub-image change.");

  return true;
}

std::string CDImageM3u::GetSubImageMetadata(u32 index, std::string_view type) const
{
  if (index >= m_entries.size())
    return {};

  if (type == "title")
    return m_entries[index].title;
  else if (type == "file_title")
    return std::string(Path::GetFileTitle(m_entries[index].filename));

  return CDImage::GetSubImageMetadata(index, type);
}

bool CDImageM3u::ReadSectorFromIndex(void* buffer, const Index& index, LBA lba_in_index)
{
  return m_current_image->ReadSectorFromIndex(buffer, index, lba_in_index);
}

bool CDImageM3u::ReadSubChannelQ(SubChannelQ* subq, const Index& index, LBA lba_in_index)
{
  return m_current_image->ReadSubChannelQ(subq, index, lba_in_index);
}

std::unique_ptr<CDImage> CDImage::OpenM3uImage(const char* filename, bool apply_patches, Error* error)
{
  std::unique_ptr<CDImageM3u> image = std::make_unique<CDImageM3u>();
  if (!image->Open(filename, apply_patches, error))
    return {};

  return image;
}
