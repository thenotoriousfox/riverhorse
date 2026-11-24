// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "common/types.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// What a host publishes about its session on the relay. Shared by Riverhorse and riverhorse-relay.
namespace NetplayListing {

static constexpr size_t MAX_NICKNAME_LENGTH = 32;
static constexpr size_t MAX_TITLE_LENGTH = 96;
static constexpr size_t MAX_SERIAL_LENGTH = 16;
static constexpr size_t MAX_VERSION_LENGTH = 48;

struct Listing
{
  u32 protocol_version = 0;
  u32 save_state_version = 0;
  u32 players = 0;
  u32 max_players = 0;
  bool has_password = false;
  std::string nickname;
  std::string title;
  std::string serial;
  std::string version;
};

inline std::vector<u8> Encode(const Listing& listing)
{
  std::vector<u8> data;
  const auto put_u32 = [&data](u32 value) {
    for (u32 i = 0; i < 4; i++)
      data.push_back(static_cast<u8>(value >> (i * 8)));
  };
  const auto put_string = [&data](std::string_view value, size_t max_length) {
    value = value.substr(0, max_length);
    data.push_back(static_cast<u8>(value.size()));
    data.insert(data.end(), value.begin(), value.end());
  };

  put_u32(listing.protocol_version);
  put_u32(listing.save_state_version);
  data.push_back(static_cast<u8>(listing.players));
  data.push_back(static_cast<u8>(listing.max_players));
  data.push_back(static_cast<u8>(listing.has_password));
  put_string(listing.nickname, MAX_NICKNAME_LENGTH);
  put_string(listing.title, MAX_TITLE_LENGTH);
  put_string(listing.serial, MAX_SERIAL_LENGTH);
  put_string(listing.version, MAX_VERSION_LENGTH);
  return data;
}

inline std::optional<Listing> Decode(std::span<const u8> data)
{
  size_t pos = 0;
  const auto get_u32 = [&data, &pos]() -> std::optional<u32> {
    if (pos + 4 > data.size())
      return std::nullopt;
    u32 value = 0;
    for (u32 i = 0; i < 4; i++)
      value |= static_cast<u32>(data[pos++]) << (i * 8);
    return value;
  };
  const auto get_u8 = [&data, &pos]() -> std::optional<u8> {
    return (pos < data.size()) ? std::optional<u8>(data[pos++]) : std::nullopt;
  };
  const auto get_string = [&data, &pos, &get_u8]() -> std::optional<std::string> {
    const std::optional<u8> length = get_u8();
    if (!length.has_value() || pos + length.value() > data.size())
      return std::nullopt;
    std::string value(reinterpret_cast<const char*>(&data[pos]), length.value());
    pos += length.value();
    return value;
  };

  const std::optional<u32> protocol_version = get_u32(), save_state_version = get_u32();
  const std::optional<u8> players = get_u8(), max_players = get_u8(), flags = get_u8();
  std::optional<std::string> nickname = get_string(), title = get_string(), serial = get_string();
  if (!protocol_version.has_value() || !save_state_version.has_value() || !players.has_value() ||
      !max_players.has_value() || !flags.has_value() || !nickname.has_value() || !title.has_value() ||
      !serial.has_value())
  {
    return std::nullopt;
  }

  Listing listing;
  listing.protocol_version = protocol_version.value();
  listing.save_state_version = save_state_version.value();
  listing.players = players.value();
  listing.max_players = max_players.value();
  listing.has_password = (flags.value() & 1) != 0;
  listing.nickname = std::move(nickname.value());
  listing.title = std::move(title.value());
  listing.serial = std::move(serial.value());
  if (std::optional<std::string> version = get_string(); version.has_value())
    listing.version = std::move(version.value());
  return listing;
}

} // namespace NetplayListing
