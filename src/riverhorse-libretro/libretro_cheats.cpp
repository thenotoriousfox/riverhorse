// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "libretro_cheats.h"
#include "libretro_core_options.h"

#include "core/cheats.h"
#include "core/system.h"

#include "common/log.h"

#include "fmt/format.h"

#include <xxhash.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

Log_SetChannel(LibretroCheats);

namespace LibretroCheats {
namespace {
struct DatabaseCode
{
  std::string option_key;
  CheatCode code;
};
} // namespace

static std::vector<DatabaseCode> s_database_codes;
static std::vector<std::string> s_frontend_codes;
static std::vector<bool> s_frontend_enabled;
static std::optional<std::vector<std::string>> s_applied_codes;

static std::string MakeOptionKey(const CheatCode& code, const std::unordered_set<std::string>& used_keys)
{
  const std::string name = fmt::format("{}/{}", code.group, code.description);
  u32 hash = XXH32(name.data(), name.size(), 0);
  std::string key;
  do
  {
    key = fmt::format("riverhorse_cheat_{:08x}", hash++);
  } while (used_keys.contains(key));
  return key;
}

static bool IsOptionEnabled(retro_environment_t environment, const std::string& key)
{
  retro_variable variable = {key.c_str(), nullptr};
  return environment(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value &&
         std::strcmp(variable.value, "true") == 0;
}

void LoadGame(retro_environment_t environment, std::string_view serial)
{
  s_database_codes.clear();
  s_applied_codes.reset();

  CheatList database;
  if (!serial.empty())
    database.LoadFromPackage(std::string(serial), "chtdb.txt");

  std::vector<LibretroCoreOptions::CheatOption> options;
  std::unordered_set<std::string> used_keys;
  for (u32 i = 0; i < database.GetCodeCount(); i++)
  {
    const CheatCode& code = database.GetCode(i);
    if (!code.Valid() || code.IsManuallyActivated())
      continue;

    std::string key = MakeOptionKey(code, used_keys);
    used_keys.insert(key);
    options.push_back({key, code.description, code.comments});
    s_database_codes.push_back({std::move(key), code});
  }

  INFO_LOG("{} cheat database codes for {}", s_database_codes.size(), serial);
  LibretroCoreOptions::SetCheatOptions(environment, std::move(options));
}

void UnloadGame(retro_environment_t environment)
{
  s_database_codes.clear();
  s_applied_codes.reset();
  LibretroCoreOptions::SetCheatOptions(environment, {});
}

void ClearFrontendCodes()
{
  s_frontend_codes.clear();
  s_frontend_enabled.clear();
}

void SetFrontendCode(unsigned index, bool enabled, const char* code)
{
  if (index >= s_frontend_codes.size())
  {
    s_frontend_codes.resize(index + 1);
    s_frontend_enabled.resize(index + 1);
  }

  s_frontend_codes[index] = code ? code : "";
  s_frontend_enabled[index] = enabled;
}

void Apply(retro_environment_t environment)
{
  if (!System::IsValid())
    return;

  std::vector<std::string> enabled_codes;
  for (size_t i = 0; i < s_frontend_codes.size(); i++)
  {
    if (s_frontend_enabled[i] && !s_frontend_codes[i].empty())
      enabled_codes.push_back(s_frontend_codes[i]);
  }
  for (const DatabaseCode& database_code : s_database_codes)
  {
    if (IsOptionEnabled(environment, database_code.option_key))
      enabled_codes.push_back(database_code.option_key);
  }
  if (s_applied_codes == enabled_codes)
    return;
  s_applied_codes = enabled_codes;

  std::unique_ptr<CheatList> list = std::make_unique<CheatList>();
  for (size_t i = 0; i < s_frontend_codes.size(); i++)
  {
    if (!s_frontend_enabled[i] || s_frontend_codes[i].empty())
      continue;

    std::string instructions = s_frontend_codes[i];
    std::replace(instructions.begin(), instructions.end(), '+', '\n');

    CheatCode code;
    code.description = fmt::format("Cheat {}", i + 1);
    code.enabled = true;
    if (!code.SetInstructionsFromString(instructions))
    {
      WARNING_LOG("Ignoring cheat {}: '{}' is not a GameShark code.", i + 1, s_frontend_codes[i]);
      continue;
    }

    list->AddCode(std::move(code));
  }

  for (const DatabaseCode& database_code : s_database_codes)
  {
    if (!IsOptionEnabled(environment, database_code.option_key))
      continue;

    CheatCode code = database_code.code;
    code.enabled = true;
    list->AddCode(std::move(code));
  }

  System::SetCheatList(list->GetCodeCount() > 0 ? std::move(list) : nullptr);
}

} // namespace LibretroCheats
