// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "libretro.h"

#include "common/types.h"

class SettingsInterface;

/// Maps the frontend's input ports onto the emulated controllers.
namespace LibretroInput {

void SetPollCallback(retro_input_poll_t input_poll);
void SetStateCallback(retro_input_state_t input_state);
void RegisterControllers(retro_environment_t environment);

/// Returns false if the port doesn't exist.
bool SetPortDevice(u32 port, unsigned device);
void ApplyPortDevices(SettingsInterface& si);

void Update();

} // namespace LibretroInput
