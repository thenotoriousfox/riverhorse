// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "common/types.h"

#include <optional>
#include <string_view>

struct _ENetHost;
class Error;

namespace NetplayRelay {

static constexpr u16 DEFAULT_PORT = 31210;
static constexpr u8 MAGIC[4] = {'R', 'H', 'R', 'L'};
static constexpr u32 HEADER_SIZE = 5;
static constexpr u32 ADDRESSED_HEADER_SIZE = 7;
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
};

bool Attach(_ENetHost* host, std::string_view relay_address, Error* error);
void Detach();
void Poll();
std::optional<u16> GetVirtualPort();

} // namespace NetplayRelay
