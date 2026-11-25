// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "libretro.h"

/// Shows Riverhorse's on-screen messages, such as disc changes and patch notices, through the frontend.
namespace LibretroMessages {

void Initialize(retro_environment_t environment);
void Shutdown();

/// Sends the messages posted since the last call. Call once per frame from retro_run().
void Flush(bool show);

} // namespace LibretroMessages
