// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "common/types.h"

#include "libretro.h"

namespace LibretroVulkan {

bool Setup(retro_environment_t environment, retro_hw_context_reset_t context_reset,
           retro_hw_context_reset_t context_destroy);
void Shutdown();
void OnContextReset(retro_environment_t environment);
bool RenderFrame(u32* width, u32* height);
void DestroyFrameResources();

} // namespace LibretroVulkan
