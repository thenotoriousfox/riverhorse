// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <ctime>
#include <optional>
#include <span>
#include <string_view>

namespace LibretroResources {

struct File
{
  std::span<const unsigned char> data;
  std::time_t timestamp;
};

std::optional<File> Find(std::string_view name);

} // namespace LibretroResources
