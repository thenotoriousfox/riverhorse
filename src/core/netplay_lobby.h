// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "netplay.h"

#include <string>
#include <vector>

/// Session list for the Big Picture netplay lobby, refreshed in the background while the lobby is open.
namespace NetplayLobby {
struct Session
{
  Netplay::SessionListing listing;

  /// Path of the matching game in the library, empty when the player does not have it.
  std::string game_path;
};

void Open();
void Close();
void Refresh();

/// Starts a refresh when the list is older than the refresh interval.
void Update();

bool IsRefreshing();
bool HasSessionList();
const std::vector<Session>& GetSessions();

/// Why the last refresh failed, empty when it worked.
const std::string& GetError();
} // namespace NetplayLobby
