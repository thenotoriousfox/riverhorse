// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

/// Scrubbable strip of rewind snapshots, drawn by the fullscreen UI over the paused game.
namespace RewindTimeline {
bool Open();
bool IsOpen();
void Draw();

/// Closes the timeline, continuing from the snapshot being shown.
void Close();
} // namespace RewindTimeline
