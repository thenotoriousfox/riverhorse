// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "libretro.h"

#include "common/types.h"

#include <span>

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

struct Crosshair
{
  u32 player;
  float x;
  float y;
};

/// Light gun aim positions on screen, 0..1 across the picture, for drawing crosshairs.
std::span<const Crosshair> GetCrosshairs();

} // namespace LibretroInput
