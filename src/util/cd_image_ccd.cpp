// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "cd_image.h"
#include "cd_subchannel_replacement.h"

#include "common/error.h"
#include "common/file_system.h"
#include "common/log.h"
#include "common/path.h"
#include "common/string_util.h"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <vector>

Log_SetChannel(CDImageCCD);

namespace {

class CDImageCCD : public CDImage
{
public:
  CDImageCCD() = default;
  ~CDImageCCD() override;

  bool Open(const char* filename, Error* error);

  bool ReadSubChannelQ(SubChannelQ* subq, const Index& index, LBA lba_in_index) override;
  bool HasNonStandardSubchannel() const override;
  bool LoadSubchannelReplacementFile(std::string_view path) override;
  s64 GetSizeOnDisk() const override;

protected:
  bool ReadSectorFromIndex(void* buffer, const Index& index, LBA lba_in_index) override;

private:
  using Section = std::map<std::string, std::string>;

  struct CCDTrack
  {
    u32 number;
    LBA index0;
    LBA index1;
    TrackMode mode;
    SubChannelQ::Control control;
  };

  static constexpr u32 SUBCODE_BYTES_PER_SECTOR = 96;

  static std::string ToLowerASCII(std::string_view text);
  static std::map<std::string, Section> ParseSections(std::string_view text);
  static std::optional<s32> GetSectionInt(const Section& section, const char* key);
  bool BuildTOC(const std::map<std::string, Section>& sections, u32 img_sectors, Error* error);
  bool SeekImg(u64 position);

  std::FILE* m_img_fp = nullptr;
  std::FILE* m_sub_fp = nullptr;
  u64 m_img_position = 0;
  CDSubChannelReplacement m_sbi;
};

} // namespace

CDImageCCD::~CDImageCCD()
{
  if (m_sub_fp)
    std::fclose(m_sub_fp);
  if (m_img_fp)
    std::fclose(m_img_fp);
}

std::string CDImageCCD::ToLowerASCII(std::string_view text)
{
  std::string result(text);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](char ch) { return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch; });
  return result;
}

std::map<std::string, CDImageCCD::Section> CDImageCCD::ParseSections(std::string_view text)
{
  std::map<std::string, Section> sections;
  Section* current = nullptr;
  while (!text.empty())
  {
    const size_t line_end = text.find_first_of("\r\n");
    std::string_view line = StringUtil::StripWhitespace(text.substr(0, line_end));
    text = (line_end == std::string_view::npos) ? std::string_view() : text.substr(line_end + 1);
    if (line.empty() || line.front() == ';')
      continue;

    if (line.front() == '[' && line.back() == ']')
    {
      current = &sections[ToLowerASCII(StringUtil::StripWhitespace(line.substr(1, line.size() - 2)))];
      continue;
    }

    const size_t equals = line.find('=');
    if (!current || equals == std::string_view::npos)
      continue;

    (*current)[ToLowerASCII(StringUtil::StripWhitespace(line.substr(0, equals)))] =
      std::string(StringUtil::StripWhitespace(line.substr(equals + 1)));
  }

  return sections;
}

std::optional<s32> CDImageCCD::GetSectionInt(const Section& section, const char* key)
{
  const auto it = section.find(key);
  if (it == section.end())
    return std::nullopt;

  const std::string_view value = it->second;
  if (value.starts_with("0x") || value.starts_with("0X"))
    return StringUtil::FromChars<s32>(value.substr(2), 16);

  return StringUtil::FromChars<s32>(value);
}

bool CDImageCCD::BuildTOC(const std::map<std::string, Section>& sections, u32 img_sectors, Error* error)
{
  std::vector<CCDTrack> tracks;
  std::optional<LBA> lead_out;

  for (const auto& [name, section] : sections)
  {
    if (!name.starts_with("entry "))
      continue;

    const std::optional<s32> session = GetSectionInt(section, "session");
    const std::optional<s32> point = GetSectionInt(section, "point");
    const std::optional<s32> plba = GetSectionInt(section, "plba");
    if (!point.has_value() || !plba.has_value() || session.value_or(1) != 1)
      continue;

    if (point.value() == 0xA2)
    {
      lead_out = static_cast<LBA>(plba.value());
      continue;
    }

    if (point.value() < 1 || point.value() > 99 || plba.value() < 0)
      continue;

    const u8 adr = static_cast<u8>(GetSectionInt(section, "adr").value_or(1) & 0x0F);
    const u8 control = static_cast<u8>(GetSectionInt(section, "control").value_or(0) & 0x0F);

    CCDTrack track = {};
    track.number = static_cast<u32>(point.value());
    track.index1 = static_cast<LBA>(plba.value());
    track.index0 = track.index1;
    track.control.bits = static_cast<u8>((control << 4) | adr);

    TrackMode mode = track.control.data ? TrackMode::Mode2Raw : TrackMode::Audio;
    if (const auto track_section = sections.find(fmt::format("track {}", track.number));
        track_section != sections.end())
    {
      if (const std::optional<s32> track_mode = GetSectionInt(track_section->second, "mode"); track_mode.has_value())
        mode = (track_mode.value() == 0) ? TrackMode::Audio :
                                           ((track_mode.value() == 1) ? TrackMode::Mode1Raw : TrackMode::Mode2Raw);
      if (const std::optional<s32> index0 = GetSectionInt(track_section->second, "index 0");
          index0.has_value() && index0.value() >= 0 && static_cast<LBA>(index0.value()) <= track.index1)
      {
        track.index0 = static_cast<LBA>(index0.value());
      }
    }
    track.mode = mode;
    tracks.push_back(track);
  }

  if (tracks.empty())
  {
    Error::SetStringView(error, "No tracks found in CloneCD control file.");
    return false;
  }

  std::sort(tracks.begin(), tracks.end(), [](const CCDTrack& a, const CCDTrack& b) { return a.number < b.number; });

  const LBA image_end = std::min(lead_out.value_or(img_sectors), img_sectors);
  const SubchannelMode submode = m_sub_fp ? SubchannelMode::Raw : SubchannelMode::None;
  const u32 pregap_frames = 2 * FRAMES_PER_SECOND;

  Index first_pregap = {};
  first_pregap.start_lba_on_disc = 0;
  first_pregap.start_lba_in_track = static_cast<LBA>(-static_cast<s32>(pregap_frames));
  first_pregap.length = pregap_frames;
  first_pregap.track_number = tracks[0].number;
  first_pregap.index_number = 0;
  first_pregap.mode = tracks[0].mode;
  first_pregap.submode = SubchannelMode::None;
  first_pregap.control = tracks[0].control;
  first_pregap.is_pregap = true;
  m_indices.push_back(first_pregap);

  for (size_t i = 0; i < tracks.size(); i++)
  {
    const CCDTrack& track = tracks[i];
    const LBA track_end = (i + 1 < tracks.size()) ? tracks[i + 1].index0 : image_end;
    if (track.index1 >= track_end)
    {
      Error::SetStringFmt(error, "Track {} in CloneCD control file is empty or out of order.", track.number);
      return false;
    }

    Index index = {};
    index.file_index = 0;
    index.file_sector_size = RAW_SECTOR_SIZE;
    index.track_number = track.number;
    index.mode = track.mode;
    index.submode = submode;
    index.control = track.control;

    const u32 first_index = static_cast<u32>(m_indices.size());
    if (track.index0 < track.index1)
    {
      index.file_offset = static_cast<u64>(track.index0) * RAW_SECTOR_SIZE;
      index.start_lba_on_disc = track.index0 + pregap_frames;
      index.start_lba_in_track = static_cast<LBA>(-static_cast<s32>(track.index1 - track.index0));
      index.length = track.index1 - track.index0;
      index.index_number = 0;
      index.is_pregap = true;
      m_indices.push_back(index);
    }

    index.file_offset = static_cast<u64>(track.index1) * RAW_SECTOR_SIZE;
    index.start_lba_on_disc = track.index1 + pregap_frames;
    index.start_lba_in_track = 0;
    index.length = track_end - track.index1;
    index.index_number = 1;
    index.is_pregap = false;
    m_indices.push_back(index);

    const u32 pregap_length = (i == 0) ? pregap_frames : (track.index1 - track.index0);
    m_tracks.push_back(Track{track.number, track.index1 + pregap_frames, first_index, index.length + pregap_length,
                             track.mode, submode, track.control});
  }

  m_lba_count = image_end + pregap_frames;
  AddLeadOutIndex();
  return true;
}

bool CDImageCCD::Open(const char* filename, Error* error)
{
  std::optional<std::string> ccd_text = FileSystem::ReadFileToString(filename, error);
  if (!ccd_text.has_value())
  {
    Error::AddPrefixFmt(error, "Failed to read '{}': ", Path::GetFileName(filename));
    return false;
  }

  const std::string img_path = Path::ReplaceExtension(filename, "img");
  m_img_fp = FileSystem::OpenSharedCFile(img_path.c_str(), "rb", FileSystem::FileShareMode::DenyWrite, error);
  if (!m_img_fp)
  {
    Error::AddPrefixFmt(error, "Failed to open '{}': ", Path::GetFileName(img_path));
    return false;
  }

  const std::string sub_path = Path::ReplaceExtension(filename, "sub");
  if (FileSystem::FileExists(sub_path.c_str()))
    m_sub_fp = FileSystem::OpenSharedCFile(sub_path.c_str(), "rb", FileSystem::FileShareMode::DenyWrite, nullptr);

  const s64 img_size = FileSystem::FSize64(m_img_fp);
  if (img_size <= 0 || (img_size % RAW_SECTOR_SIZE) != 0)
  {
    Error::SetStringFmt(error, "'{}' is not a whole number of 2352-byte sectors.", Path::GetFileName(img_path));
    return false;
  }

  m_filename = filename;
  if (!BuildTOC(ParseSections(ccd_text.value()), static_cast<u32>(img_size / RAW_SECTOR_SIZE), error))
    return false;

  m_sbi.LoadFromImagePath(filename);
  return Seek(1, Position{0, 0, 0});
}

bool CDImageCCD::SeekImg(u64 position)
{
  if (m_img_position == position)
    return true;

  if (FileSystem::FSeek64(m_img_fp, static_cast<s64>(position), SEEK_SET) != 0)
    return false;

  m_img_position = position;
  return true;
}

bool CDImageCCD::ReadSectorFromIndex(void* buffer, const Index& index, LBA lba_in_index)
{
  const u64 position = index.file_offset + static_cast<u64>(lba_in_index) * RAW_SECTOR_SIZE;
  if (!SeekImg(position) || std::fread(buffer, RAW_SECTOR_SIZE, 1, m_img_fp) != 1)
  {
    m_img_position = UINT64_MAX;
    return false;
  }

  m_img_position += RAW_SECTOR_SIZE;
  return true;
}

bool CDImageCCD::ReadSubChannelQ(SubChannelQ* subq, const Index& index, LBA lba_in_index)
{
  if (m_sbi.GetReplacementSubChannelQ(index.start_lba_on_disc + lba_in_index, subq))
    return true;

  if (index.submode == SubchannelMode::None || index.file_sector_size == 0)
    return CDImage::ReadSubChannelQ(subq, index, lba_in_index);

  const u64 sector = index.file_offset / RAW_SECTOR_SIZE + static_cast<u64>(lba_in_index);
  const s64 position = static_cast<s64>(sector * SUBCODE_BYTES_PER_SECTOR + SUBCHANNEL_BYTES_PER_FRAME);
  if (FileSystem::FSeek64(m_sub_fp, position, SEEK_SET) != 0 ||
      std::fread(subq->data.data(), SUBCHANNEL_BYTES_PER_FRAME, 1, m_sub_fp) != 1)
  {
    return CDImage::ReadSubChannelQ(subq, index, lba_in_index);
  }

  return true;
}

bool CDImageCCD::LoadSubchannelReplacementFile(std::string_view path)
{
  return m_sbi.LoadFromFile(path);
}

bool CDImageCCD::HasNonStandardSubchannel() const
{
  return (m_sub_fp != nullptr || m_sbi.GetReplacementSectorCount() > 0);
}

s64 CDImageCCD::GetSizeOnDisk() const
{
  return FileSystem::FSize64(m_img_fp) + (m_sub_fp ? FileSystem::FSize64(m_sub_fp) : 0);
}

std::unique_ptr<CDImage> CDImage::OpenCCDImage(const char* filename, Error* error)
{
  std::unique_ptr<CDImageCCD> image = std::make_unique<CDImageCCD>();
  if (!image->Open(filename, error))
    return {};

  return image;
}
