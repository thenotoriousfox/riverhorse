// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "netplay_relay.h"

#include "common/error.h"
#include "common/log.h"
#include "common/string_util.h"
#include "common/timer.h"

#include "enet/enet.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

Log_SetChannel(NetplayRelay);

namespace NetplayRelay {

static constexpr float REGISTER_RETRY_SECONDS = 1.0f;

static void SendControlMessage(MessageType type);
static int ENET_CALLBACK SendThroughRelay(ENetHost* host, const ENetAddress* address, const ENetBuffer* buffers,
                                          size_t buffer_count);
static int ENET_CALLBACK ReceiveFromRelay(ENetHost* host, ENetEvent* event);

static ENetHost* s_host = nullptr;
static ENetAddress s_relay_address = {};
static std::optional<u16> s_virtual_port;
static Common::Timer s_last_message_time;

} // namespace NetplayRelay

static ENetBuffer MakeBuffer(void* data, size_t size)
{
  ENetBuffer buffer;
  buffer.data = data;
  buffer.dataLength = size;
  return buffer;
}

static void WriteHeader(u8* data, NetplayRelay::MessageType type)
{
  std::memcpy(data, NetplayRelay::MAGIC, sizeof(NetplayRelay::MAGIC));
  data[4] = static_cast<u8>(type);
}

void NetplayRelay::SendControlMessage(MessageType type)
{
  std::array<u8, HEADER_SIZE> data;
  WriteHeader(data.data(), type);
  const ENetBuffer buffer = MakeBuffer(data.data(), data.size());
  enet_socket_send(s_host->socket, &s_relay_address, &buffer, 1);
  s_last_message_time.Reset();
}

int ENET_CALLBACK NetplayRelay::SendThroughRelay(ENetHost* host, const ENetAddress* address,
                                                 const ENetBuffer* buffers, size_t buffer_count)
{
  if (address->host != s_relay_address.host)
    return 0;

  std::array<u8, ADDRESSED_HEADER_SIZE + MAX_PAYLOAD_SIZE> data;
  WriteHeader(data.data(), MessageType::Send);
  data[5] = static_cast<u8>(address->port >> 8);
  data[6] = static_cast<u8>(address->port);

  size_t size = ADDRESSED_HEADER_SIZE;
  for (size_t i = 0; i < buffer_count; i++)
  {
    if (size + buffers[i].dataLength > data.size())
      return -1;

    std::memcpy(&data[size], buffers[i].data, buffers[i].dataLength);
    size += buffers[i].dataLength;
  }

  const ENetBuffer buffer = MakeBuffer(data.data(), size);
  const int sent = enet_socket_send(host->socket, &s_relay_address, &buffer, 1);
  if (sent > 0)
    s_last_message_time.Reset();
  return (sent < 0) ? sent : std::max(sent - static_cast<int>(ADDRESSED_HEADER_SIZE), 0);
}

int ENET_CALLBACK NetplayRelay::ReceiveFromRelay(ENetHost* host, ENetEvent* event)
{
  const u8* const data = host->receivedData;
  const size_t size = host->receivedDataLength;
  if (host->receivedAddress.host != s_relay_address.host || host->receivedAddress.port != s_relay_address.port ||
      size < ADDRESSED_HEADER_SIZE || std::memcmp(data, MAGIC, sizeof(MAGIC)) != 0)
  {
    return 1;
  }

  const u16 port = static_cast<u16>((data[5] << 8) | data[6]);
  switch (static_cast<MessageType>(data[4]))
  {
    case MessageType::Registered:
    {
      if (s_virtual_port != port)
        INFO_LOG("Registered with relay, virtual port {}.", port);
      s_virtual_port = port;
      return 1;
    }

    case MessageType::Receive:
    {
      host->receivedAddress.port = port;
      host->receivedData += ADDRESSED_HEADER_SIZE;
      host->receivedDataLength -= ADDRESSED_HEADER_SIZE;
      return 0;
    }

    default:
      return 1;
  }
}

bool NetplayRelay::Attach(ENetHost* host, std::string_view relay_address, Error* error)
{
  std::string_view relay_host = relay_address;
  u16 relay_port = DEFAULT_PORT;
  if (const std::string_view::size_type colon = relay_address.rfind(':'); colon != std::string_view::npos)
  {
    const std::optional<u16> port = StringUtil::FromChars<u16>(relay_address.substr(colon + 1));
    if (!port.has_value() || port.value() == 0)
    {
      Error::SetStringFmt(error, "Invalid relay port in '{}'.", relay_address);
      return false;
    }

    relay_host = relay_address.substr(0, colon);
    relay_port = port.value();
  }

  ENetAddress address = {};
  address.port = relay_port;
  if (enet_address_set_host(&address, std::string(relay_host).c_str()) != 0)
  {
    Error::SetStringFmt(error, "Could not resolve relay server '{}'.", relay_host);
    return false;
  }

  s_host = host;
  s_relay_address = address;
  s_virtual_port.reset();
  host->send = SendThroughRelay;
  host->intercept = ReceiveFromRelay;
  SendControlMessage(MessageType::Register);
  return true;
}

void NetplayRelay::Detach()
{
  if (!s_host)
    return;

  s_host->send = nullptr;
  s_host->intercept = nullptr;
  s_host = nullptr;
  s_virtual_port.reset();
}

void NetplayRelay::Poll()
{
  if (!s_host)
    return;

  const double seconds = s_last_message_time.GetTimeSeconds();
  if (!s_virtual_port.has_value() && seconds >= REGISTER_RETRY_SECONDS)
    SendControlMessage(MessageType::Register);
  else if (seconds >= KEEPALIVE_INTERVAL_SECONDS)
    SendControlMessage(MessageType::KeepAlive);
}

std::optional<u16> NetplayRelay::GetVirtualPort()
{
  return s_virtual_port;
}
