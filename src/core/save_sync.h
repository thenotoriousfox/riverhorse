// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "common/types.h"

#include <optional>

class Error;

/// Keeps memory cards, save states and per-game settings the same on every device, through a folder that another
/// program syncs or a WebDAV server.
namespace SaveSync {

enum class Mode : u8
{
  Disabled,
  Folder,
  WebDAV,
  Count
};

std::optional<Mode> ParseModeName(const char* name);
const char* GetModeName(Mode mode);
const char* GetModeDisplayName(Mode mode);

struct Result
{
  u32 uploaded = 0;
  u32 downloaded = 0;
  u32 deleted = 0;

  /// Files changed on both sides. Both versions are kept; the older one gets "(conflict ...)" in its name.
  u32 conflicts = 0;
};

bool IsEnabled();

/// Syncs every chosen kind of file now. Don't call while a game is running.
bool SyncNow(Result* result, Error* error);

/// Syncs if enabled and shows the outcome on screen. Called at startup, before a game boots and after it shuts down.
void SyncAndReport();

} // namespace SaveSync
