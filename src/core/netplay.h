// SPDX-FileCopyrightText: 2023 Connor McLaughlin <stenzek@gmail.com> and contributors.
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "types.h"
#include <string>

class SettingsInterface;

namespace Netplay {

enum : s32
{
  // Maximum number of players. Players 3 and 4 need a multitap in the host's controller settings.
  MAX_PLAYERS = 4,

  // Maximum number of spectators allowed to watch the session.
  MAX_SPECTATORS = 4,

  // Maximum netplay prediction frames
  MAX_ROLLBACK_FRAMES = 8,

  // Maximum length of a nickname
  MAX_NICKNAME_LENGTH = 128,

  // Maximum name of password for session
  MAX_SESSION_PASSWORD_LENGTH = 128,

  DEFAULT_PORT = 31200,
};

static constexpr const char* DEFAULT_NICKNAME = "Player";

enum : u8
{
  ENET_CHANNEL_CONTROL = 0,
  ENET_CHANNEL_GGPO = 1,

  NUM_ENET_CHANNELS,
};

bool CreateSession(std::string nickname, s32 port, s32 max_players, std::string password, int inputdelay);
/// game_path, if set, is used when it holds the host's game; otherwise the game list is searched.
bool JoinSession(std::string nickname, const std::string& hostname, s32 port, std::string password, bool spectating,
                 int inputdelay, std::string game_path = {});

bool IsActive();

/// Writes the settings every rollback session needs: no fastmem, block linking, runahead, rewind or threaded
/// rendering, and devices restored from memory states.
void SetRollbackSettings(SettingsInterface& si);

/// Frees up resources associated with the current netplay session.
/// Should only be called by System::ShutdownSystem().
void SystemDestroyed();

/// Runs the VM and netplay loop. when the netplay loop cancels it switches to normal execute mode.
void ExecuteNetplay();

/// Called when a frame is completed (System::FrameDone()).
void FrameDone();

void CollectInput(u32 slot, u32 bind, float value);

void SendChatMessage(const std::string_view& msg);

s32 GetPing();
u32 GetMaxPrediction();

/// Updates the throttle period, call when target emulation speed changes.
void UpdateThrottlePeriod();

void ToggleDesyncNotifications();

// OSD
void RenderOverlays();
void OpenChat();

} // namespace Netplay

namespace Host {
void OnNetplaySessionOpened(bool is_host);
void OnNetplaySessionClosed();
} // namespace Host
