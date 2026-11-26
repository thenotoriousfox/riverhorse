// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "netplay_lobby.h"
#include "game_list.h"
#include "host.h"

#include "common/error.h"
#include "common/timer.h"

#include <optional>
#include <thread>

namespace NetplayLobby {
static constexpr float REFRESH_SECONDS = 10.0f;

static void ApplySessionList(std::optional<std::vector<Netplay::SessionListing>> listings, std::string error);

static bool s_open = false;
static bool s_refreshing = false;
static bool s_has_session_list = false;
static u32 s_generation = 0;
static Common::Timer s_refresh_timer;
static std::vector<Session> s_sessions;
static std::string s_error;
} // namespace NetplayLobby

void NetplayLobby::Open()
{
  s_open = true;
  s_has_session_list = false;
  s_sessions.clear();
  s_error.clear();
  Refresh();
}

void NetplayLobby::Close()
{
  s_open = false;
  s_generation++;
  s_refreshing = false;
  s_sessions.clear();
}

void NetplayLobby::Refresh()
{
  if (!s_open || s_refreshing)
    return;

  s_refreshing = true;
  s_refresh_timer.Reset();
  std::thread([generation = s_generation]() {
    Error error;
    std::optional<std::vector<Netplay::SessionListing>> listings = Netplay::GetSessionList(&error);
    Host::RunOnCPUThread([generation, listings = std::move(listings), error = error.GetDescription()]() mutable {
      if (generation == s_generation)
        ApplySessionList(std::move(listings), std::move(error));
    });
  }).detach();
}

void NetplayLobby::ApplySessionList(std::optional<std::vector<Netplay::SessionListing>> listings, std::string error)
{
  s_refreshing = false;
  s_refresh_timer.Reset();
  if (!listings.has_value())
  {
    s_error = std::move(error);
    return;
  }

  s_error.clear();
  s_has_session_list = true;
  s_sessions.clear();

  const auto lock = GameList::GetLock();
  for (Netplay::SessionListing& listing : listings.value())
  {
    const GameList::Entry* entry = GameList::GetEntryBySerial(listing.game_serial);
    s_sessions.push_back(Session{std::move(listing), entry ? entry->path : std::string()});
  }
}

void NetplayLobby::Update()
{
  if (s_open && !s_refreshing && s_refresh_timer.GetTimeSeconds() >= REFRESH_SECONDS)
    Refresh();
}

bool NetplayLobby::IsRefreshing()
{
  return s_refreshing;
}

bool NetplayLobby::HasSessionList()
{
  return s_has_session_list;
}

const std::vector<NetplayLobby::Session>& NetplayLobby::GetSessions()
{
  return s_sessions;
}

const std::string& NetplayLobby::GetError()
{
  return s_error;
}
