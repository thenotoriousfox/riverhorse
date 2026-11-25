// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "libretro.h"

#include <string_view>

/// Combines the frontend's GameShark cheats with the cheat database codes picked in the core options.
namespace LibretroCheats {

/// Offers the game's cheat database codes as core options. Call after the game has booted.
void LoadGame(retro_environment_t environment, std::string_view serial);
void UnloadGame(retro_environment_t environment);

void ClearFrontendCodes();
void SetFrontendCode(unsigned index, bool enabled, const char* code);

/// Gives the console the cheats that are switched on now.
void Apply(retro_environment_t environment);

} // namespace LibretroCheats
