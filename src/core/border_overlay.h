// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "common/gsvector.h"
#include "common/types.h"

/// Border overlay (bezel): an image drawn around the game, with the game shown in its transparent middle.
namespace BorderOverlay {

/// Loads the image named in the settings when it changed. Call on the CPU thread before drawing.
void Update();

/// Area of a window this size the game is shown in, or the whole window without an overlay.
GSVector4i GetScreenRect(s32 window_width, s32 window_height);

/// Draws the overlay over the game in ImGui's background layer.
void Draw();

} // namespace BorderOverlay
