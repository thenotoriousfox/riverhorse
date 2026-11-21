// SPDX-FileCopyrightText: 2019-2024 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only

#include "cd_image.h"
#include "cd_subchannel_replacement.h"

#include "common/error.h"
#include "common/file_system.h"
#include "common/path.h"

#include <array>
#include <cstring>

namespace {

class CDImageBin : public CDImage
{
public:
  CDImageBin();
  ~CDImageBin() override;

  bool Open(const char* filename, Error* error);

  bool ReadSubChannelQ(SubChannelQ* subq, const Index& index, LBA lba_in_index) override;
  bool HasNonStandardSubchannel() const override;
  bool LoadSubchannelReplacementFile(std::string_view path) override;

  s64 GetSizeOnDisk() const override;

protected:
  bool ReadSectorFromIndex(void* buffer, const Index& index, LBA lba_in_index) override;

private:
  static TrackMode DetectTrackMode(const std::array<u8, SECTOR_SYNC_SIZE + SECTOR_HEADER_SIZE>& first_sector_header,
                                   u64 file_size);

  std::FILE* m_fp = nullptr;
  u64 m_file_position = 0;

  CDSubChannelReplacement m_sbi;
};

} // namespace

CDImageBin::CDImageBin() = default;

CDImageBin::~CDImageBin()
{
  if (m_fp)
    std::fclose(m_fp);
}

CDImage::TrackMode
CDImageBin::DetectTrackMode(const std::array<u8, SECTOR_SYNC_SIZE + SECTOR_HEADER_SIZE>& first_sector_header,
                            u64 file_size)
{
  const bool raw_size = (file_size % RAW_SECTOR_SIZE) == 0;
  const u8 sector_mode = first_sector_header[SECTOR_SYNC_SIZE + 3];
  if (raw_size &&
      std::memcmp(first_sector_header.data(), SECTOR_SYNC_PATTERN.data(), SECTOR_SYNC_PATTERN.size()) == 0 &&
      (sector_mode == 1 || sector_mode == 2))
  {
    return (sector_mode == 1) ? TrackMode::Mode1Raw : TrackMode::Mode2Raw;
  }

  return raw_size ? TrackMode::Audio : TrackMode::Mode1;
}

bool CDImageBin::Open(const char* filename, Error* error)
{
  m_filename = filename;
  m_fp = FileSystem::OpenSharedCFile(filename, "rb", FileSystem::FileShareMode::DenyWrite, error);
  if (!m_fp)
  {
    Error::AddPrefixFmt(error, "Failed to open binfile '{}': ", Path::GetFileName(filename));
    return false;
  }

  const s64 file_size = FileSystem::FSize64(m_fp);
  std::array<u8, SECTOR_SYNC_SIZE + SECTOR_HEADER_SIZE> first_sector_header;
  if (file_size < static_cast<s64>(first_sector_header.size()) ||
      std::fread(first_sector_header.data(), first_sector_header.size(), 1, m_fp) != 1)
  {
    Error::SetStringFmt(error, "Unable to determine the sector format of '{}'", Path::GetFileName(filename));
    return false;
  }
  std::fseek(m_fp, 0, SEEK_SET);

  const TrackMode mode = DetectTrackMode(first_sector_header, static_cast<u64>(file_size));
  const u32 track_sector_size = GetBytesPerSector(mode);
  m_lba_count = static_cast<u32>(static_cast<u64>(file_size) / track_sector_size);

  SubChannelQ::Control control = {};
  control.data = mode != TrackMode::Audio;

  // Two seconds default pregap.
  const u32 pregap_frames = 2 * FRAMES_PER_SECOND;
  Index pregap_index = {};
  pregap_index.file_sector_size = track_sector_size;
  pregap_index.start_lba_on_disc = 0;
  pregap_index.start_lba_in_track = static_cast<LBA>(-static_cast<s32>(pregap_frames));
  pregap_index.length = pregap_frames;
  pregap_index.track_number = 1;
  pregap_index.index_number = 0;
  pregap_index.mode = mode;
  pregap_index.submode = CDImage::SubchannelMode::None;
  pregap_index.control.bits = control.bits;
  pregap_index.is_pregap = true;
  m_indices.push_back(pregap_index);

  // Data index.
  Index data_index = {};
  data_index.file_index = 0;
  data_index.file_offset = 0;
  data_index.file_sector_size = track_sector_size;
  data_index.start_lba_on_disc = pregap_index.length;
  data_index.track_number = 1;
  data_index.index_number = 1;
  data_index.start_lba_in_track = 0;
  data_index.length = m_lba_count;
  data_index.mode = mode;
  data_index.submode = CDImage::SubchannelMode::None;
  data_index.control.bits = control.bits;
  m_indices.push_back(data_index);

  // Assume a single track.
  m_tracks.push_back(Track{static_cast<u32>(1), data_index.start_lba_on_disc, static_cast<u32>(0), m_lba_count, mode,
                           SubchannelMode::None, control});

  AddLeadOutIndex();

  m_sbi.LoadFromImagePath(filename);

  return Seek(1, Position{0, 0, 0});
}

bool CDImageBin::ReadSubChannelQ(SubChannelQ* subq, const Index& index, LBA lba_in_index)
{
  if (m_sbi.GetReplacementSubChannelQ(index.start_lba_on_disc + lba_in_index, subq))
    return true;

  return CDImage::ReadSubChannelQ(subq, index, lba_in_index);
}

bool CDImageBin::LoadSubchannelReplacementFile(std::string_view path)
{
  return m_sbi.LoadFromFile(path);
}

bool CDImageBin::HasNonStandardSubchannel() const
{
  return (m_sbi.GetReplacementSectorCount() > 0);
}

bool CDImageBin::ReadSectorFromIndex(void* buffer, const Index& index, LBA lba_in_index)
{
  const u64 file_position = index.file_offset + (static_cast<u64>(lba_in_index) * index.file_sector_size);
  if (m_file_position != file_position)
  {
    if (std::fseek(m_fp, static_cast<long>(file_position), SEEK_SET) != 0)
      return false;

    m_file_position = file_position;
  }

  if (std::fread(buffer, index.file_sector_size, 1, m_fp) != 1)
  {
    std::fseek(m_fp, static_cast<long>(m_file_position), SEEK_SET);
    return false;
  }

  m_file_position += index.file_sector_size;
  return true;
}

s64 CDImageBin::GetSizeOnDisk() const
{
  return FileSystem::FSize64(m_fp);
}

std::unique_ptr<CDImage> CDImage::OpenBinImage(const char* filename, Error* error)
{
  std::unique_ptr<CDImageBin> image = std::make_unique<CDImageBin>();
  if (!image->Open(filename, error))
    return {};

  return image;
}
