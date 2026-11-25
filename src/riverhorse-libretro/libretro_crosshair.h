// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "common/types.h"

class GPUTexture;

/// Draws the light guns' aim onto the frame the core sends.
namespace LibretroCrosshair {

void Draw(GPUTexture* target);
void Draw(u32* xrgb_pixels, u32 width, u32 height);
void DestroyResources();

} // namespace LibretroCrosshair
