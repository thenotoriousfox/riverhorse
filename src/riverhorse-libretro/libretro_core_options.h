// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "libretro.h"

class SettingsInterface;

namespace LibretroCoreOptions {

void Register(retro_environment_t environment);
void Apply(retro_environment_t environment, SettingsInterface& si);
bool HaveChanged(retro_environment_t environment);

} // namespace LibretroCoreOptions
