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
#include <unordered_map>

Log_SetChannel(NetplayRelay);

namespace NetplayRelay {

static constexpr float REGISTER_RETRY_SECONDS = 1.0f;
static constexpr float LOCATE_RETRY_SECONDS = 2.0f;
static constexpr u32 LOCATE_ATTEMPTS = 5;
static constexpr float PUNCH_INTERVAL_SECONDS = 0.2f;
static constexpr float PUNCH_TIMEOUT_SECONDS = 10.0f;
static constexpr float DIRECT_TIMEOUT_SECONDS = 5.0f;

struct Peer
{
  std::array<ENetAddress, 2> candidates = {};
  ENetAddress endpoint = {};
  bool introduced = false;
  bool direct = false;
  u32 locate_attempts = 0;
  Common::Timer last_locate;
  Common::Timer introduced_at;
  Common::Timer last_punch;
  Common::Timer last_direct_receive;
};

static void SendControlMessage(MessageType type);
static void Register();
static void Punch(const Peer& peer);
static ENetAddress ReadEndpoint(const u8* data);
static ENetAddress FindLocalAddress(ENetHost* host, const ENetAddress& relay_address);
static void SendToAddress(const ENetAddress& address, const u8* data, size_t size);
static void SendAddressed(const ENetAddress& address, MessageType type, u16 port);
static void Locate(u16 port, Peer& peer);
static int ENET_CALLBACK SendThroughRelay(ENetHost* host, const ENetAddress* address, const ENetBuffer* buffers,
                                          size_t buffer_count);
static int ENET_CALLBACK ReceiveFromRelay(ENetHost* host, ENetEvent* event);
static int ReceiveFromRelayServer(ENetHost* host, const u8* data, size_t size);
static int ReceiveFromPeer(ENetHost* host, const u8* data, size_t size);

static ENetHost* s_host = nullptr;
static ENetAddress s_relay_address = {};
static ENetAddress s_local_address = {};
static std::optional<u16> s_virtual_port;
static Common::Timer s_last_message_time;
static std::unordered_map<u16, Peer> s_peers;

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

static void WritePort(u8* data, u16 port)
{
  data[0] = static_cast<u8>(port >> 8);
  data[1] = static_cast<u8>(port);
}

static u16 ReadPort(const u8* data)
{
  return static_cast<u16>((data[0] << 8) | data[1]);
}

static bool SameAddress(const ENetAddress& a, const ENetAddress& b)
{
  return a.host == b.host && a.port == b.port;
}

void NetplayRelay::SendToAddress(const ENetAddress& address, const u8* data, size_t size)
{
  const ENetBuffer buffer = MakeBuffer(const_cast<u8*>(data), size);
  enet_socket_send(s_host->socket, &address, &buffer, 1);
}

void NetplayRelay::SendControlMessage(MessageType type)
{
  std::array<u8, HEADER_SIZE> data;
  WriteHeader(data.data(), type);
  SendToAddress(s_relay_address, data.data(), data.size());
  s_last_message_time.Reset();
}

ENetAddress NetplayRelay::ReadEndpoint(const u8* data)
{
  ENetAddress address = {};
  std::memcpy(&address.host, data, sizeof(address.host));
  address.port = ReadPort(&data[4]);
  return address;
}

void NetplayRelay::Register()
{
  std::array<u8, HEADER_SIZE + ENDPOINT_SIZE> data;
  WriteHeader(data.data(), MessageType::Register);
  std::memcpy(&data[HEADER_SIZE], &s_local_address.host, sizeof(s_local_address.host));
  WritePort(&data[HEADER_SIZE + 4], s_local_address.port);
  SendToAddress(s_relay_address, data.data(), data.size());
  s_last_message_time.Reset();
}

void NetplayRelay::Punch(const Peer& peer)
{
  for (const ENetAddress& candidate : peer.candidates)
  {
    if (candidate.host != 0)
      SendAddressed(candidate, MessageType::Punch, s_virtual_port.value_or(0));
  }
}

void NetplayRelay::SendAddressed(const ENetAddress& address, MessageType type, u16 port)
{
  std::array<u8, ADDRESSED_HEADER_SIZE> data;
  WriteHeader(data.data(), type);
  WritePort(&data[5], port);
  SendToAddress(address, data.data(), data.size());
}

void NetplayRelay::Locate(u16 port, Peer& peer)
{
  SendAddressed(s_relay_address, MessageType::Locate, port);
  s_last_message_time.Reset();
  peer.locate_attempts++;
  peer.last_locate.Reset();
}

int ENET_CALLBACK NetplayRelay::SendThroughRelay(ENetHost* host, const ENetAddress* address,
                                                 const ENetBuffer* buffers, size_t buffer_count)
{
  if (address->host != s_relay_address.host)
    return 0;

  const auto [it, inserted] = s_peers.try_emplace(address->port);
  Peer& peer = it->second;
  if (inserted)
    Locate(address->port, peer);

  std::array<u8, ADDRESSED_HEADER_SIZE + MAX_PAYLOAD_SIZE> data;
  const u32 header_size = peer.direct ? HEADER_SIZE : ADDRESSED_HEADER_SIZE;
  WriteHeader(data.data(), peer.direct ? MessageType::Direct : MessageType::Send);
  if (!peer.direct)
    WritePort(&data[5], address->port);

  size_t size = header_size;
  for (size_t i = 0; i < buffer_count; i++)
  {
    if (size + buffers[i].dataLength > data.size())
      return -1;

    std::memcpy(&data[size], buffers[i].data, buffers[i].dataLength);
    size += buffers[i].dataLength;
  }

  const ENetBuffer buffer = MakeBuffer(data.data(), size);
  const int sent = enet_socket_send(host->socket, peer.direct ? &peer.endpoint : &s_relay_address, &buffer, 1);
  if (sent > 0 && !peer.direct)
    s_last_message_time.Reset();
  return (sent < 0) ? sent : std::max(sent - static_cast<int>(header_size), 0);
}

int NetplayRelay::ReceiveFromRelayServer(ENetHost* host, const u8* data, size_t size)
{
  if (size < ADDRESSED_HEADER_SIZE)
    return 1;

  const u16 port = ReadPort(&data[5]);
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

    case MessageType::Introduce:
    {
      if (size < INTRODUCE_SIZE)
        return 1;

      Peer& peer = s_peers[port];
      const std::array<ENetAddress, 2> candidates = {ReadEndpoint(&data[ADDRESSED_HEADER_SIZE]),
                                                     ReadEndpoint(&data[ADDRESSED_HEADER_SIZE + ENDPOINT_SIZE])};
      if (peer.direct && std::any_of(candidates.begin(), candidates.end(), [&peer](const ENetAddress& candidate) {
            return SameAddress(candidate, peer.endpoint);
          }))
      {
        return 1;
      }

      DEV_LOG("Relay introduced virtual port {}, trying a direct connection.", port);
      peer.candidates = candidates;
      peer.introduced = true;
      peer.direct = false;
      peer.introduced_at.Reset();
      Punch(peer);
      peer.last_punch.Reset();
      return 1;
    }

    default:
      return 1;
  }
}

int NetplayRelay::ReceiveFromPeer(ENetHost* host, const u8* data, size_t size)
{
  const ENetAddress from = host->receivedAddress;
  const auto it = std::find_if(s_peers.begin(), s_peers.end(), [&from](const auto& entry) {
    return entry.second.introduced &&
           std::any_of(entry.second.candidates.begin(), entry.second.candidates.end(),
                       [&from](const ENetAddress& candidate) { return SameAddress(candidate, from); });
  });
  if (it == s_peers.end())
    return 1;

  const u16 port = it->first;
  Peer& peer = it->second;
  switch (static_cast<MessageType>(data[4]))
  {
    case MessageType::Punch:
    {
      SendAddressed(from, MessageType::PunchAck, s_virtual_port.value_or(0));
      return 1;
    }

    case MessageType::PunchAck:
    {
      if (!peer.direct)
        INFO_LOG("Direct connection to virtual port {}.", port);
      peer.endpoint = from;
      peer.direct = true;
      peer.last_direct_receive.Reset();
      return 1;
    }

    case MessageType::Direct:
    {
      peer.last_direct_receive.Reset();
      host->receivedAddress = s_relay_address;
      host->receivedAddress.port = port;
      host->receivedData += HEADER_SIZE;
      host->receivedDataLength -= HEADER_SIZE;
      return 0;
    }

    default:
      return 1;
  }
}

int ENET_CALLBACK NetplayRelay::ReceiveFromRelay(ENetHost* host, ENetEvent* event)
{
  const u8* const data = host->receivedData;
  const size_t size = host->receivedDataLength;
  if (size < HEADER_SIZE || std::memcmp(data, MAGIC, sizeof(MAGIC)) != 0)
    return 1;

  return SameAddress(host->receivedAddress, s_relay_address) ? ReceiveFromRelayServer(host, data, size) :
                                                               ReceiveFromPeer(host, data, size);
}

ENetAddress NetplayRelay::FindLocalAddress(ENetHost* host, const ENetAddress& relay_address)
{
  ENetAddress local = {};
  enet_socket_get_address(host->socket, &local);

  const ENetSocket probe = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
  ENetAddress route = {};
  if (probe != ENET_SOCKET_NULL && enet_socket_connect(probe, &relay_address) == 0 &&
      enet_socket_get_address(probe, &route) == 0)
  {
    local.host = route.host;
  }
  if (probe != ENET_SOCKET_NULL)
    enet_socket_destroy(probe);

  return local;
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
  s_local_address = FindLocalAddress(host, address);
  s_virtual_port.reset();
  s_peers.clear();
  host->send = SendThroughRelay;
  host->intercept = ReceiveFromRelay;
  Register();
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
  s_peers.clear();
}

void NetplayRelay::Poll()
{
  if (!s_host)
    return;

  const double seconds = s_last_message_time.GetTimeSeconds();
  if (!s_virtual_port.has_value() && seconds >= REGISTER_RETRY_SECONDS)
    Register();
  else if (seconds >= KEEPALIVE_INTERVAL_SECONDS)
    SendControlMessage(MessageType::KeepAlive);

  for (auto& [port, peer] : s_peers)
  {
    if (peer.direct)
    {
      if (peer.last_direct_receive.GetTimeSeconds() < DIRECT_TIMEOUT_SECONDS)
        continue;

      WARNING_LOG("Direct connection to virtual port {} went quiet, back to the relay.", port);
      peer.direct = false;
      peer.introduced = false;
      peer.locate_attempts = 0;
      Locate(port, peer);
    }
    else if (!peer.introduced)
    {
      if (peer.locate_attempts < LOCATE_ATTEMPTS && peer.last_locate.GetTimeSeconds() >= LOCATE_RETRY_SECONDS)
        Locate(port, peer);
    }
    else if (peer.introduced_at.GetTimeSeconds() < PUNCH_TIMEOUT_SECONDS &&
             peer.last_punch.GetTimeSeconds() >= PUNCH_INTERVAL_SECONDS)
    {
      Punch(peer);
      peer.last_punch.Reset();
    }
  }
}

std::optional<u16> NetplayRelay::GetVirtualPort()
{
  return s_virtual_port;
}
