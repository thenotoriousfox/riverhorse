// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "libretro.h"

/// Forwards the emulated controllers' vibration motors to the frontend's rumble interface.
namespace LibretroRumble {

void Initialize(retro_environment_t environment);
void Shutdown();

/// Registers the motors of every pad with InputManager. Call from Host::AddFixedInputBindings().
void AddBindings();

} // namespace LibretroRumble
