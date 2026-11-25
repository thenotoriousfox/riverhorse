// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "common/types.h"

#include <ctime>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class Error;

namespace SaveSync {

struct RemoteFile
{
  s64 size;
  std::time_t modified;

  /// Changes whenever the file's contents change on the remote. Empty when it can't be told yet, such as for a
  /// file written in the last seconds of a timestamp that only counts whole seconds.
  std::string tag;
};

using RemoteListing = std::map<std::string, RemoteFile, std::less<>>;

/// True for a timestamp so recent that a later change in the same second would leave it the same.
bool IsTimestampTooRecent(std::time_t modified);

/// Where synced files are kept: a folder, or a WebDAV server. Directories are one level below the root.
class Backend
{
public:
  virtual ~Backend();

  /// Lists the files in a directory. A directory that doesn't exist yet is empty.
  virtual bool ListFiles(std::string_view directory, RemoteListing* files, Error* error) = 0;
  virtual std::optional<std::vector<u8>> ReadFile(std::string_view directory, std::string_view name, Error* error) = 0;

  /// Creates the directory if it doesn't exist yet.
  virtual bool WriteFile(std::string_view directory, std::string_view name, std::span<const u8> data, Error* error) = 0;
  virtual bool DeleteFile(std::string_view directory, std::string_view name, Error* error) = 0;

  static std::unique_ptr<Backend> CreateFolder(std::string root);
  static std::unique_ptr<Backend> CreateWebDAV(std::string url, std::string_view username, std::string_view password);
};

} // namespace SaveSync
