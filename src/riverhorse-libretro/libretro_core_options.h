// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "libretro.h"

#include <string>
#include <vector>

class SettingsInterface;

namespace LibretroCoreOptions {

/// An on/off option for one of the loaded game's cheat codes, off by default.
struct CheatOption
{
  std::string key;
  std::string description;
  std::string info;
};

void Register(retro_environment_t environment);

/// Replaces the cheat options of the previous game and registers the options again.
void SetCheatOptions(retro_environment_t environment, std::vector<CheatOption> options);

void Apply(retro_environment_t environment, SettingsInterface& si);
bool HaveChanged(retro_environment_t environment);

} // namespace LibretroCoreOptions
