// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "libretro.h"

/// Decides which frames the core can leave out of its output, to keep up on slow devices and while fast-forwarding.
namespace LibretroFrameSkip {

void Initialize(retro_environment_t environment, bool can_dupe_frames);
void Shutdown();

/// Turns automatic frameskip on or off. When on, frames are dropped while the frontend's audio buffer runs low.
void SetAutomatic(bool enabled);

/// Returns true when this frame's picture should not be sent. Call once per frame.
bool ShouldSkipFrame();

} // namespace LibretroFrameSkip
