// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "libretro_resources.h"

#include <algorithm>
#include <cstddef>

namespace {

struct EmbeddedFile
{
  std::string_view name;
  const unsigned char* begin;
  const unsigned char* end;
  std::time_t timestamp;
};

#include "libretro_embedded_resources.inc"

} // namespace

std::optional<LibretroResources::File> LibretroResources::Find(std::string_view name)
{
  const auto it = std::lower_bound(std::begin(s_embedded_files), std::end(s_embedded_files), name,
                                   [](const EmbeddedFile& file, std::string_view key) { return file.name < key; });
  if (it == std::end(s_embedded_files) || it->name != name)
    return std::nullopt;

  return File{std::span<const unsigned char>(it->begin, it->end), it->timestamp};
}
