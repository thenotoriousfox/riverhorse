// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "core/netplay_relay.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <unordered_map>

using namespace NetplayRelay;
using Clock = std::chrono::steady_clock;

static constexpr size_t MAX_CLIENTS = 4096;
static constexpr u16 FIRST_VIRTUAL_PORT = 1024;

struct Client
{
  sockaddr_in address;
  u16 virtual_port;
  Clock::time_point last_seen;
};

static u64 EndpointKey(const sockaddr_in& address)
{
  return (static_cast<u64>(address.sin_addr.s_addr) << 16) | address.sin_port;
}

static void WriteHeader(u8* data, MessageType type, u16 port)
{
  std::memcpy(data, MAGIC, sizeof(MAGIC));
  data[4] = static_cast<u8>(type);
  data[5] = static_cast<u8>(port >> 8);
  data[6] = static_cast<u8>(port);
}

int main(int argc, char* argv[])
{
  const int listen_port = (argc > 1) ? std::atoi(argv[1]) : DEFAULT_PORT;
  if (listen_port <= 0 || listen_port > 65535)
  {
    std::fprintf(stderr, "Usage: %s [port]\nRelays Riverhorse netplay traffic. Default port %u (UDP).\n", argv[0],
                 DEFAULT_PORT);
    return 1;
  }

  const int fd = socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in bind_address = {};
  bind_address.sin_family = AF_INET;
  bind_address.sin_addr.s_addr = htonl(INADDR_ANY);
  bind_address.sin_port = htons(static_cast<u16>(listen_port));
  if (fd < 0 || bind(fd, reinterpret_cast<const sockaddr*>(&bind_address), sizeof(bind_address)) != 0)
  {
    std::perror("bind");
    return 1;
  }

  std::printf("Riverhorse netplay relay listening on UDP port %d.\n", listen_port);
  std::fflush(stdout);

  std::unordered_map<u64, Client> clients;
  std::unordered_map<u16, u64> clients_by_port;
  std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<u32> port_distribution(FIRST_VIRTUAL_PORT, 65535);
  Clock::time_point last_expiry = Clock::now();

  std::array<u8, ADDRESSED_HEADER_SIZE + MAX_PAYLOAD_SIZE> packet;
  std::array<u8, ADDRESSED_HEADER_SIZE + MAX_PAYLOAD_SIZE> forward;
  for (;;)
  {
    pollfd pfd = {fd, POLLIN, 0};
    poll(&pfd, 1, 1000);

    const Clock::time_point now = Clock::now();
    if (now - last_expiry >= std::chrono::seconds(KEEPALIVE_INTERVAL_SECONDS))
    {
      for (auto it = clients.begin(); it != clients.end();)
      {
        if (now - it->second.last_seen < std::chrono::seconds(CLIENT_TIMEOUT_SECONDS))
        {
          ++it;
          continue;
        }

        std::printf("Expired virtual port %u.\n", it->second.virtual_port);
        clients_by_port.erase(it->second.virtual_port);
        it = clients.erase(it);
      }
      std::fflush(stdout);
      last_expiry = now;
    }

    if (!(pfd.revents & POLLIN))
      continue;

    sockaddr_in from = {};
    socklen_t from_size = sizeof(from);
    const ssize_t size =
      recvfrom(fd, packet.data(), packet.size(), 0, reinterpret_cast<sockaddr*>(&from), &from_size);
    if (size < static_cast<ssize_t>(HEADER_SIZE) || std::memcmp(packet.data(), MAGIC, sizeof(MAGIC)) != 0)
      continue;

    const MessageType type = static_cast<MessageType>(packet[4]);
    if (type != MessageType::Register && type != MessageType::Send && type != MessageType::KeepAlive)
      continue;

    const u64 key = EndpointKey(from);
    auto it = clients.find(key);
    if (it == clients.end())
    {
      if (clients.size() >= MAX_CLIENTS)
        continue;

      u16 virtual_port;
      do
        virtual_port = static_cast<u16>(port_distribution(rng));
      while (clients_by_port.contains(virtual_port));

      it = clients.emplace(key, Client{from, virtual_port, now}).first;
      clients_by_port.emplace(virtual_port, key);
      std::printf("Registered virtual port %u.\n", virtual_port);
      std::fflush(stdout);
    }

    Client& client = it->second;
    client.last_seen = now;

    if (type == MessageType::Register)
    {
      WriteHeader(forward.data(), MessageType::Registered, client.virtual_port);
      sendto(fd, forward.data(), ADDRESSED_HEADER_SIZE, 0, reinterpret_cast<const sockaddr*>(&client.address),
             sizeof(client.address));
    }
    else if (type == MessageType::Send && size > static_cast<ssize_t>(ADDRESSED_HEADER_SIZE))
    {
      const u16 destination_port = static_cast<u16>((packet[5] << 8) | packet[6]);
      const auto destination = clients_by_port.find(destination_port);
      if (destination == clients_by_port.end())
        continue;

      const Client& target = clients.at(destination->second);
      const size_t payload_size = static_cast<size_t>(size) - ADDRESSED_HEADER_SIZE;
      WriteHeader(forward.data(), MessageType::Receive, client.virtual_port);
      std::memcpy(&forward[ADDRESSED_HEADER_SIZE], &packet[ADDRESSED_HEADER_SIZE], payload_size);
      sendto(fd, forward.data(), ADDRESSED_HEADER_SIZE + payload_size, 0,
             reinterpret_cast<const sockaddr*>(&target.address), sizeof(target.address));
    }
  }
}
