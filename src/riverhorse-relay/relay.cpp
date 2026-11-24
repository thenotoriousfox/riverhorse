// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "core/netplay_listing.h"
#include "core/netplay_relay.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <random>
#include <unordered_map>
#include <vector>

using namespace NetplayRelay;
using Clock = std::chrono::steady_clock;

static constexpr size_t MAX_CLIENTS = 4096;
static constexpr u16 FIRST_VIRTUAL_PORT = 1024;
static constexpr u32 BATCH_SIZE = 64;

struct Client
{
  sockaddr_in address;
  sockaddr_in local_address;
  u16 virtual_port;
  Clock::time_point last_seen;
  std::vector<u8> listing;
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

struct PublicSession
{
  std::string version;
  std::string json;
};

static std::mutex s_public_sessions_mutex;
static std::vector<PublicSession> s_public_sessions;

static void AppendJsonString(std::string* out, std::string_view value)
{
  out->push_back('"');
  for (const char ch : value)
  {
    if (ch == '"' || ch == '\\')
    {
      out->push_back('\\');
      out->push_back(ch);
    }
    else if (static_cast<unsigned char>(ch) < 0x20)
    {
      char escaped[8];
      std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(ch)));
      out->append(escaped);
    }
    else
    {
      out->push_back(ch);
    }
  }
  out->push_back('"');
}

static void UpdatePublicSessions(const std::unordered_map<u64, Client>& clients)
{
  std::vector<PublicSession> sessions;
  for (const auto& [key, client] : clients)
  {
    if (client.listing.empty())
      continue;

    const std::optional<NetplayListing::Listing> listing = NetplayListing::Decode(client.listing);
    if (!listing.has_value())
      continue;

    std::string json = "{\"id\":" + std::to_string(client.virtual_port) +
                       ",\"code\":" + std::to_string(client.virtual_port) + ",\"nickname\":";
    AppendJsonString(&json, listing->nickname);
    json += ",\"serial\":";
    AppendJsonString(&json, listing->serial);
    json += ",\"title\":";
    AppendJsonString(&json, listing->title);
    json += ",\"version\":";
    AppendJsonString(&json, listing->version);
    json += ",\"protocol\":" + std::to_string(listing->protocol_version) +
            ",\"state_version\":" + std::to_string(listing->save_state_version) +
            ",\"players\":" + std::to_string(listing->players) +
            ",\"max_players\":" + std::to_string(listing->max_players) +
            ",\"password\":" + (listing->has_password ? "true" : "false") +
            ",\"joinable\":" + ((listing->players < listing->max_players) ? "true" : "false") + "}";
    sessions.push_back(PublicSession{listing->version, std::move(json)});
  }

  std::unique_lock lock(s_public_sessions_mutex);
  s_public_sessions = std::move(sessions);
}

static std::string DecodeQueryValue(std::string_view value)
{
  std::string out;
  for (size_t i = 0; i < value.size(); i++)
  {
    if (value[i] == '+')
      out.push_back(' ');
    else if (value[i] == '%' && i + 2 < value.size())
    {
      out.push_back(static_cast<char>(std::strtol(std::string(value.substr(i + 1, 2)).c_str(), nullptr, 16)));
      i += 2;
    }
    else
      out.push_back(value[i]);
  }
  return out;
}

static std::string BuildHttpResponse(std::string_view request)
{
  const auto respond = [](std::string_view status, std::string_view body) {
    return "HTTP/1.1 " + std::string(status) +
           "\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\n"
           "Connection: close\r\nContent-Length: " +
           std::to_string(body.size()) + "\r\n\r\n" + std::string(body);
  };

  const size_t line_end = request.find("\r\n");
  const std::string_view line = request.substr(0, line_end);
  if (!line.starts_with("GET "))
    return respond("405 Method Not Allowed", "{\"error\":\"method not allowed\"}");

  const size_t target_end = line.find(' ', 4);
  const std::string_view target = line.substr(4, (target_end == std::string_view::npos) ? line.size() - 4 : target_end - 4);
  const size_t query_start = target.find('?');
  const std::string_view path = target.substr(0, query_start);
  if (path != "/v1/sessions")
    return respond("404 Not Found", "{\"error\":\"not found\"}");

  std::string version;
  if (query_start != std::string_view::npos)
  {
    std::string_view query = target.substr(query_start + 1);
    while (!query.empty())
    {
      const size_t amp = query.find('&');
      const std::string_view pair = query.substr(0, amp);
      if (pair.starts_with("version="))
        version = DecodeQueryValue(pair.substr(8));
      query = (amp == std::string_view::npos) ? std::string_view() : query.substr(amp + 1);
    }
  }

  std::string body = "{\"sessions\":[";
  {
    std::unique_lock lock(s_public_sessions_mutex);
    bool first = true;
    for (const PublicSession& session : s_public_sessions)
    {
      if (!version.empty() && session.version != version)
        continue;
      if (!first)
        body.push_back(',');
      body += session.json;
      first = false;
    }
  }
  body += "]}";
  return respond("200 OK", body);
}

static void ServeHttp(int listen_port)
{
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  const int reuse = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(static_cast<u16>(listen_port));
  if (fd < 0 || bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 || listen(fd, 16) != 0)
  {
    std::perror("HTTP listen");
    return;
  }

  std::printf("Session list on http://0.0.0.0:%d/v1/sessions.\n", listen_port);
  std::fflush(stdout);

  const timeval timeout = {2, 0};
  for (;;)
  {
    const int client = accept(fd, nullptr, nullptr);
    if (client < 0)
      continue;

    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    std::string request;
    char buffer[1024];
    while (request.size() < 8192 && request.find("\r\n\r\n") == std::string::npos)
    {
      const ssize_t received = recv(client, buffer, sizeof(buffer), 0);
      if (received <= 0)
        break;
      request.append(buffer, static_cast<size_t>(received));
    }

    const std::string response = BuildHttpResponse(request);
    send(client, response.data(), response.size(), MSG_NOSIGNAL);
    close(client);
  }
}

static void SendSessionList(int fd, const sockaddr_in& to, u16 start,
                            const std::unordered_map<u64, Client>& clients)
{
  std::vector<const Client*> listed;
  for (const auto& [key, client] : clients)
  {
    if (!client.listing.empty())
      listed.push_back(&client);
  }
  std::sort(listed.begin(), listed.end(),
            [](const Client* a, const Client* b) { return a->virtual_port < b->virtual_port; });
  if (listed.size() > MAX_LISTED_SESSIONS)
    listed.resize(MAX_LISTED_SESSIONS);

  std::array<u8, LIST_PACKET_SIZE> data;
  WriteHeader(data.data(), MessageType::ListResponse, static_cast<u16>(listed.size()));
  data[7] = static_cast<u8>(start >> 8);
  data[8] = static_cast<u8>(start);
  size_t size = LIST_RESPONSE_HEADER_SIZE;
  u8 count = 0;
  for (size_t i = start; i < listed.size(); i++)
  {
    const Client& client = *listed[i];
    if (size + 3 + client.listing.size() > data.size())
      break;

    data[size++] = static_cast<u8>(client.virtual_port >> 8);
    data[size++] = static_cast<u8>(client.virtual_port);
    data[size++] = static_cast<u8>(client.listing.size());
    std::memcpy(&data[size], client.listing.data(), client.listing.size());
    size += client.listing.size();
    count++;
  }
  data[9] = count;

  sendto(fd, data.data(), size, 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
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
  std::thread(ServeHttp, listen_port).detach();
  bool listings_changed = false;

  std::unordered_map<u64, Client> clients;
  std::unordered_map<u16, u64> clients_by_port;
  std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<u32> port_distribution(FIRST_VIRTUAL_PORT, 65535);
  Clock::time_point last_expiry = Clock::now();

  struct Datagram
  {
    std::array<u8, ADDRESSED_HEADER_SIZE + MAX_PAYLOAD_SIZE> data;
    sockaddr_in address;
    iovec buffer;
  };
  std::vector<Datagram> received(BATCH_SIZE);
  std::array<mmsghdr, BATCH_SIZE> receive_headers = {};
  for (u32 i = 0; i < BATCH_SIZE; i++)
  {
    received[i].buffer = {received[i].data.data(), received[i].data.size()};
    receive_headers[i].msg_hdr.msg_name = &received[i].address;
    receive_headers[i].msg_hdr.msg_iov = &received[i].buffer;
    receive_headers[i].msg_hdr.msg_iovlen = 1;
  }

  std::array<sockaddr_in, BATCH_SIZE * 2> send_addresses;
  std::array<iovec, BATCH_SIZE * 2> send_buffers;
  std::array<mmsghdr, BATCH_SIZE * 2> send_headers = {};
  std::array<std::array<u8, INTRODUCE_SIZE>, BATCH_SIZE * 2> introductions;

  const timeval receive_timeout = {1, 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout, sizeof(receive_timeout));

  for (;;)
  {
    if (listings_changed)
    {
      UpdatePublicSessions(clients);
      listings_changed = false;
    }

    for (mmsghdr& header : receive_headers)
      header.msg_hdr.msg_namelen = sizeof(sockaddr_in);
    const int count = recvmmsg(fd, receive_headers.data(), BATCH_SIZE, MSG_WAITFORONE, nullptr);

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
        listings_changed |= !it->second.listing.empty();
        clients_by_port.erase(it->second.virtual_port);
        it = clients.erase(it);
      }
      std::fflush(stdout);
      last_expiry = now;
    }

    if (count <= 0)
      continue;

    u32 send_count = 0;
    const auto queue_send = [&](u8* data, size_t size, const sockaddr_in& address) {
      send_addresses[send_count] = address;
      send_buffers[send_count] = {data, size};
      msghdr& header = send_headers[send_count].msg_hdr;
      header.msg_name = &send_addresses[send_count];
      header.msg_namelen = sizeof(sockaddr_in);
      header.msg_iov = &send_buffers[send_count];
      header.msg_iovlen = 1;
      send_count++;
    };

    for (int i = 0; i < count; i++)
    {
      u8* const packet = received[i].data.data();
      const size_t size = receive_headers[i].msg_len;
      if (size < HEADER_SIZE || std::memcmp(packet, MAGIC, sizeof(MAGIC)) != 0)
        continue;

      const MessageType type = static_cast<MessageType>(packet[4]);
      if (type == MessageType::ListRequest)
      {
        if (size >= LIST_PACKET_SIZE)
          SendSessionList(fd, received[i].address, static_cast<u16>((packet[5] << 8) | packet[6]), clients);
        continue;
      }

      if (type != MessageType::Register && type != MessageType::Send && type != MessageType::KeepAlive &&
          type != MessageType::Locate && type != MessageType::Publish && type != MessageType::Unpublish)
        continue;

      const sockaddr_in& from = received[i].address;
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

        it = clients.emplace(key, Client{from, {}, virtual_port, now}).first;
        clients_by_port.emplace(virtual_port, key);
        std::printf("Registered virtual port %u.\n", virtual_port);
        std::fflush(stdout);
      }

      Client& client = it->second;
      client.last_seen = now;

      if (type == MessageType::Register)
      {
        if (size >= HEADER_SIZE + ENDPOINT_SIZE)
        {
          client.local_address.sin_family = AF_INET;
          std::memcpy(&client.local_address.sin_addr.s_addr, &packet[HEADER_SIZE], 4);
          std::memcpy(&client.local_address.sin_port, &packet[HEADER_SIZE + 4], 2);
        }

        WriteHeader(packet, MessageType::Registered, client.virtual_port);
        queue_send(packet, ADDRESSED_HEADER_SIZE, client.address);
      }
      else if (type == MessageType::Publish)
      {
        client.listing.assign(packet + HEADER_SIZE, packet + std::min<size_t>(size, HEADER_SIZE + MAX_LISTING_SIZE));
        listings_changed = true;
      }
      else if (type == MessageType::Unpublish)
      {
        client.listing.clear();
        listings_changed = true;
      }
      else if (type == MessageType::Locate && size >= ADDRESSED_HEADER_SIZE)
      {
        const auto target = clients_by_port.find(static_cast<u16>((packet[5] << 8) | packet[6]));
        if (target == clients_by_port.end())
          continue;

        const Client& other = clients.at(target->second);
        const auto introduce = [&](const Client& to, const Client& about) {
          u8* const data = introductions[send_count].data();
          WriteHeader(data, MessageType::Introduce, about.virtual_port);
          std::memcpy(&data[ADDRESSED_HEADER_SIZE], &about.address.sin_addr.s_addr, 4);
          std::memcpy(&data[ADDRESSED_HEADER_SIZE + 4], &about.address.sin_port, 2);
          std::memcpy(&data[ADDRESSED_HEADER_SIZE + ENDPOINT_SIZE], &about.local_address.sin_addr.s_addr, 4);
          std::memcpy(&data[ADDRESSED_HEADER_SIZE + ENDPOINT_SIZE + 4], &about.local_address.sin_port, 2);
          queue_send(data, INTRODUCE_SIZE, to.address);
        };
        introduce(client, other);
        introduce(other, client);
      }
      else if (type == MessageType::Send && size > ADDRESSED_HEADER_SIZE)
      {
        const u16 destination_port = static_cast<u16>((packet[5] << 8) | packet[6]);
        const auto destination = clients_by_port.find(destination_port);
        if (destination == clients_by_port.end())
          continue;

        WriteHeader(packet, MessageType::Receive, client.virtual_port);
        queue_send(packet, size, clients.at(destination->second).address);
      }
    }

    for (u32 sent = 0; sent < send_count;)
    {
      const int result = sendmmsg(fd, &send_headers[sent], send_count - sent, 0);
      if (result <= 0)
        break;
      sent += static_cast<u32>(result);
    }
  }
}
