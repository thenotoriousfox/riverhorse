// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "save_sync_backends.h"

#include "util/http_downloader.h"

#include "common/error.h"
#include "common/file_system.h"
#include "common/path.h"
#include "common/string_util.h"

#include "fmt/format.h"

#include <algorithm>
#include <array>

namespace SaveSync {
namespace {

class FolderBackend final : public Backend
{
public:
  explicit FolderBackend(std::string root) : m_root(std::move(root)) {}

  bool ListFiles(std::string_view directory, RemoteListing* files, Error* error) override;
  std::optional<std::vector<u8>> ReadFile(std::string_view directory, std::string_view name, Error* error) override;
  bool WriteFile(std::string_view directory, std::string_view name, std::span<const u8> data, Error* error) override;
  bool DeleteFile(std::string_view directory, std::string_view name, Error* error) override;

private:
  std::string m_root;
};

class WebDAVBackend final : public Backend
{
public:
  WebDAVBackend(std::string url, std::string_view username, std::string_view password);

  bool ListFiles(std::string_view directory, RemoteListing* files, Error* error) override;
  std::optional<std::vector<u8>> ReadFile(std::string_view directory, std::string_view name, Error* error) override;
  bool WriteFile(std::string_view directory, std::string_view name, std::span<const u8> data, Error* error) override;
  bool DeleteFile(std::string_view directory, std::string_view name, Error* error) override;

private:
  struct Response
  {
    s32 status = HTTPDownloader::HTTP_STATUS_ERROR;
    std::vector<u8> data;
  };

  Response Perform(std::string method, std::string url, std::vector<std::string> extra_headers = {},
                   std::string body = {});
  bool CreateDirectories(std::string_view directory, Error* error);
  std::string GetDirectoryURL(std::string_view directory) const;
  std::string GetFileURL(std::string_view directory, std::string_view name) const;

  std::string m_url;
  std::string m_authorization;
  std::unique_ptr<HTTPDownloader> m_http;
};

} // namespace

static constexpr float WEBDAV_TIMEOUT_SECONDS = 10.0f;
static constexpr std::string_view PROPFIND_BODY =
  R"(<?xml version="1.0" encoding="utf-8"?><d:propfind xmlns:d="DAV:"><d:prop><d:resourcetype/>)"
  R"(<d:getcontentlength/><d:getlastmodified/><d:getetag/></d:prop></d:propfind>)";

static constexpr s64 TIMESTAMP_SETTLE_SECONDS = 2;

static std::string MakeTag(s64 size, std::time_t modified)
{
  return IsTimestampTooRecent(modified) ? std::string() : fmt::format("{}:{}", size, static_cast<s64>(modified));
}

static std::string DecodeXMLText(std::string_view text)
{
  static constexpr std::array<std::pair<std::string_view, char>, 5> entities = {
    {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}}};

  std::string ret;
  ret.reserve(text.size());
  for (size_t i = 0; i < text.size(); i++)
  {
    bool replaced = false;
    for (const auto& [entity, ch] : entities)
    {
      if (text.substr(i).starts_with(entity))
      {
        ret.push_back(ch);
        i += entity.size() - 1;
        replaced = true;
        break;
      }
    }
    if (!replaced)
      ret.push_back(text[i]);
  }

  return std::string(StringUtil::StripWhitespace(ret));
}

/// Returns the contents of every element with this name, whatever namespace prefix the server gives it.
static std::vector<std::string_view> FindXMLElements(std::string_view xml, std::string_view local_name)
{
  std::vector<std::string_view> elements;
  for (size_t pos = xml.find('<'); pos != std::string_view::npos; pos = xml.find('<', pos + 1))
  {
    if (pos + 1 >= xml.size() || xml[pos + 1] == '/' || xml[pos + 1] == '?' || xml[pos + 1] == '!')
      continue;

    const size_t name_end = xml.find_first_of(" \t\r\n/>", pos + 1);
    if (name_end == std::string_view::npos)
      break;

    const std::string_view qualified_name = xml.substr(pos + 1, name_end - pos - 1);
    const size_t colon = qualified_name.find(':');
    if (((colon == std::string_view::npos) ? qualified_name : qualified_name.substr(colon + 1)) != local_name)
      continue;

    const size_t tag_end = xml.find('>', name_end);
    if (tag_end == std::string_view::npos)
      break;

    if (xml[tag_end - 1] == '/')
    {
      elements.emplace_back();
      continue;
    }

    const std::string closing_tag = fmt::format("</{}>", qualified_name);
    const size_t close = xml.find(closing_tag, tag_end + 1);
    if (close == std::string_view::npos)
      break;

    elements.push_back(xml.substr(tag_end + 1, close - tag_end - 1));
    pos = close;
  }

  return elements;
}

static std::string_view FindXMLElement(std::string_view xml, std::string_view local_name)
{
  const std::vector<std::string_view> elements = FindXMLElements(xml, local_name);
  return elements.empty() ? std::string_view() : elements.front();
}

static s64 DaysFromCivil(s64 year, u32 month, u32 day)
{
  year -= (month <= 2) ? 1 : 0;
  const s64 era = (year >= 0 ? year : year - 399) / 400;
  const u32 year_of_era = static_cast<u32>(year - era * 400);
  const u32 day_of_year = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
  const u32 day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
  return era * 146097 + static_cast<s64>(day_of_era) - 719468;
}

/// Parses an HTTP date such as "Tue, 25 Nov 2026 03:10:00 GMT". Returns 0 when it isn't one.
static std::time_t ParseHTTPDate(std::string_view text)
{
  static constexpr std::array<std::string_view, 12> months = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

  const size_t comma = text.find(", ");
  if (comma == std::string_view::npos)
    return 0;

  const std::vector<std::string_view> parts = StringUtil::SplitString(text.substr(comma + 2), ' ', true);
  if (parts.size() < 4)
    return 0;

  const std::vector<std::string_view> time_parts = StringUtil::SplitString(parts[3], ':', true);
  const std::optional<u32> day = StringUtil::FromChars<u32>(parts[0]);
  const std::optional<s64> year = StringUtil::FromChars<s64>(parts[2]);
  const auto month = std::find(months.begin(), months.end(), parts[1]);
  if (!day.has_value() || !year.has_value() || month == months.end() || time_parts.size() != 3)
    return 0;

  const std::optional<u32> hours = StringUtil::FromChars<u32>(time_parts[0]);
  const std::optional<u32> minutes = StringUtil::FromChars<u32>(time_parts[1]);
  const std::optional<u32> seconds = StringUtil::FromChars<u32>(time_parts[2]);
  if (!hours.has_value() || !minutes.has_value() || !seconds.has_value())
    return 0;

  const s64 days = DaysFromCivil(year.value(), static_cast<u32>(month - months.begin()) + 1, day.value());
  return static_cast<std::time_t>(days * 86400 + hours.value() * 3600 + minutes.value() * 60 + seconds.value());
}

bool IsTimestampTooRecent(std::time_t modified)
{
  return static_cast<s64>(std::time(nullptr) - modified) < TIMESTAMP_SETTLE_SECONDS;
}

static std::string DescribeHTTPStatus(s32 status)
{
  if (status <= 0)
    return "the server couldn't be reached";
  if (status == 401 || status == 403)
    return "the server refused the username or password";

  return fmt::format("HTTP {}", status);
}

Backend::~Backend() = default;

std::unique_ptr<Backend> Backend::CreateFolder(std::string root)
{
  return std::make_unique<FolderBackend>(std::move(root));
}

std::unique_ptr<Backend> Backend::CreateWebDAV(std::string url, std::string_view username, std::string_view password)
{
  return std::make_unique<WebDAVBackend>(std::move(url), username, password);
}

bool FolderBackend::ListFiles(std::string_view directory, RemoteListing* files, Error* error)
{
  files->clear();
  const std::string path = Path::Combine(m_root, directory);
  if (!FileSystem::DirectoryExists(path.c_str()))
    return true;

  FileSystem::FindResultsArray results;
  FileSystem::FindFiles(path.c_str(), "*", FILESYSTEM_FIND_FILES | FILESYSTEM_FIND_RELATIVE_PATHS, &results);
  for (const FILESYSTEM_FIND_DATA& result : results)
  {
    files->emplace(result.FileName,
                   RemoteFile{result.Size, result.ModificationTime, MakeTag(result.Size, result.ModificationTime)});
  }

  return true;
}

std::optional<std::vector<u8>> FolderBackend::ReadFile(std::string_view directory, std::string_view name, Error* error)
{
  return FileSystem::ReadBinaryFile(Path::Combine(Path::Combine(m_root, directory), name).c_str(), error);
}

bool FolderBackend::WriteFile(std::string_view directory, std::string_view name, std::span<const u8> data, Error* error)
{
  const std::string path = Path::Combine(m_root, directory);
  return FileSystem::EnsureDirectoryExists(path.c_str(), true, error) &&
         FileSystem::WriteAtomicRenamedFile(Path::Combine(path, name), data.data(), data.size(), error);
}

bool FolderBackend::DeleteFile(std::string_view directory, std::string_view name, Error* error)
{
  const std::string path = Path::Combine(Path::Combine(m_root, directory), name);
  return !FileSystem::FileExists(path.c_str()) || FileSystem::DeleteFile(path.c_str(), error);
}

WebDAVBackend::WebDAVBackend(std::string url, std::string_view username, std::string_view password)
  : m_url(std::move(url)), m_http(HTTPDownloader::Create())
{
  if (!m_url.ends_with('/'))
    m_url.push_back('/');

  if (!username.empty())
    m_authorization =
      fmt::format("Authorization: Basic {}", StringUtil::EncodeBase64(fmt::format("{}:{}", username, password)));

  if (m_http)
    m_http->SetTimeout(WEBDAV_TIMEOUT_SECONDS);
}

WebDAVBackend::Response WebDAVBackend::Perform(std::string method, std::string url,
                                               std::vector<std::string> extra_headers, std::string body)
{
  Response response;
  if (!m_http)
    return response;

  if (!m_authorization.empty())
    extra_headers.push_back(m_authorization);

  m_http->CreateCustomRequest(std::move(method), std::move(url), std::move(extra_headers), std::move(body),
                              [&response](s32 status_code, const std::string&, HTTPDownloader::Request::Data data) {
                                response.status = status_code;
                                response.data = std::move(data);
                              });
  m_http->WaitForAllRequests();
  return response;
}

std::string WebDAVBackend::GetDirectoryURL(std::string_view directory) const
{
  return fmt::format("{}{}/", m_url, StringUtil::EncodeURLComponent(directory));
}

std::string WebDAVBackend::GetFileURL(std::string_view directory, std::string_view name) const
{
  return fmt::format("{}{}", GetDirectoryURL(directory), StringUtil::EncodeURLComponent(name));
}

bool WebDAVBackend::ListFiles(std::string_view directory, RemoteListing* files, Error* error)
{
  files->clear();
  const Response response =
    Perform("PROPFIND", GetDirectoryURL(directory), {"Depth: 1", "Content-Type: application/xml; charset=utf-8"},
            std::string(PROPFIND_BODY));
  if (response.status == 404)
    return true;
  if (response.status != 207)
  {
    Error::SetStringFmt(error, "Listing '{}' on the WebDAV server failed: {}.", directory,
                        DescribeHTTPStatus(response.status));
    return false;
  }

  const std::string_view xml(reinterpret_cast<const char*>(response.data.data()), response.data.size());
  for (const std::string_view entry : FindXMLElements(xml, "response"))
  {
    if (!FindXMLElements(FindXMLElement(entry, "resourcetype"), "collection").empty())
      continue;

    std::string href = StringUtil::DecodeURLComponent(DecodeXMLText(FindXMLElement(entry, "href")));
    while (href.ends_with('/'))
      href.pop_back();
    const std::string name = href.substr(href.rfind('/') + 1);
    if (name.empty())
      continue;

    const s64 size = StringUtil::FromChars<s64>(DecodeXMLText(FindXMLElement(entry, "getcontentlength"))).value_or(0);
    const std::time_t modified = ParseHTTPDate(DecodeXMLText(FindXMLElement(entry, "getlastmodified")));
    std::string tag = DecodeXMLText(FindXMLElement(entry, "getetag"));
    if (tag.empty())
      tag = MakeTag(size, modified);

    files->emplace(name, RemoteFile{size, modified, std::move(tag)});
  }

  return true;
}

std::optional<std::vector<u8>> WebDAVBackend::ReadFile(std::string_view directory, std::string_view name, Error* error)
{
  Response response = Perform("GET", GetFileURL(directory, name));
  if (response.status != 200)
  {
    Error::SetStringFmt(error, "Downloading '{}' from the WebDAV server failed: {}.", name,
                        DescribeHTTPStatus(response.status));
    return std::nullopt;
  }

  return std::move(response.data);
}

bool WebDAVBackend::CreateDirectories(std::string_view directory, Error* error)
{
  for (const std::string& url : {m_url, GetDirectoryURL(directory)})
  {
    const Response response = Perform("MKCOL", url);
    if (response.status != 201 && response.status != 405)
    {
      Error::SetStringFmt(error, "Creating '{}' on the WebDAV server failed: {}.", url,
                          DescribeHTTPStatus(response.status));
      return false;
    }
  }

  return true;
}

bool WebDAVBackend::WriteFile(std::string_view directory, std::string_view name, std::span<const u8> data, Error* error)
{
  const std::string body(reinterpret_cast<const char*>(data.data()), data.size());
  const std::vector<std::string> headers = {"Content-Type: application/octet-stream"};
  Response response = Perform("PUT", GetFileURL(directory, name), headers, body);
  if (response.status == 404 || response.status == 409)
  {
    if (!CreateDirectories(directory, error))
      return false;

    response = Perform("PUT", GetFileURL(directory, name), headers, body);
  }

  if (response.status != 200 && response.status != 201 && response.status != 204)
  {
    Error::SetStringFmt(error, "Uploading '{}' to the WebDAV server failed: {}.", name,
                        DescribeHTTPStatus(response.status));
    return false;
  }

  return true;
}

bool WebDAVBackend::DeleteFile(std::string_view directory, std::string_view name, Error* error)
{
  const Response response = Perform("DELETE", GetFileURL(directory, name));
  if (response.status != 200 && response.status != 204 && response.status != 404)
  {
    Error::SetStringFmt(error, "Deleting '{}' on the WebDAV server failed: {}.", name,
                        DescribeHTTPStatus(response.status));
    return false;
  }

  return true;
}

} // namespace SaveSync
