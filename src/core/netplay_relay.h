// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "common/types.h"

#include <optional>
#include <string_view>

struct _ENetHost;
struct _ENetAddress;
class Error;

namespace NetplayRelay {

static constexpr const char* DEFAULT_SERVER = "relay.hippos-linux.org";
static constexpr u16 DEFAULT_PORT = 31210;
static constexpr u8 MAGIC[4] = {'R', 'H', 'R', 'L'};
static constexpr u32 HEADER_SIZE = 5;
static constexpr u32 ADDRESSED_HEADER_SIZE = 7;
static constexpr u32 ENDPOINT_SIZE = 6;
static constexpr u32 INTRODUCE_SIZE = ADDRESSED_HEADER_SIZE + ENDPOINT_SIZE * 2;
static constexpr u32 MAX_PAYLOAD_SIZE = 4096;
static constexpr u32 KEEPALIVE_INTERVAL_SECONDS = 5;
static constexpr u32 CLIENT_TIMEOUT_SECONDS = 30;

enum class MessageType : u8
{
  Register = 1,
  Registered = 2,
  Send = 3,
  Receive = 4,
  KeepAlive = 5,
  Locate = 6,
  Introduce = 7,
  Punch = 8,
  PunchAck = 9,
  Direct = 10,
};

bool Attach(_ENetHost* host, std::string_view relay_address, Error* error);
void Detach();
void Poll();
std::optional<u16> GetVirtualPort();
bool GetSessionAddress(u16 session_code, _ENetAddress* address);

} // namespace NetplayRelay
