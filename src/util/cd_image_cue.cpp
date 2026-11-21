// SPDX-FileCopyrightText: 2019-2023 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only

#include "cd_image.h"
#include "cd_subchannel_replacement.h"
#include "cue_parser.h"

#include "common/assert.h"
#include "common/error.h"
#include "common/file_system.h"
#include "common/log.h"
#include "common/path.h"

#include "fmt/format.h"

#include <algorithm>
#include <cinttypes>
#include <cstring>
#include <map>

Log_SetChannel(CDImageCueSheet);

namespace {

class CDImageCueSheet : public CDImage
{
public:
  CDImageCueSheet();
  ~CDImageCueSheet() override;

  bool OpenAndParse(const char* filename, Error* error);

  bool ReadSubChannelQ(SubChannelQ* subq, const Index& index, LBA lba_in_index) override;
  bool HasNonStandardSubchannel() const override;
  bool LoadSubchannelReplacementFile(std::string_view path) override;
  s64 GetSizeOnDisk() const override;

protected:
  bool ReadSectorFromIndex(void* buffer, const Index& index, LBA lba_in_index) override;

private:
  struct TrackFile
  {
    std::string filename;
    std::FILE* file;
    u64 file_position;
    u64 data_offset;
    u64 data_size;
  };

  static bool FindWaveAudioData(std::FILE* fp, u64* data_offset, u64* data_size, Error* error);

  std::vector<TrackFile> m_files;
  CDSubChannelReplacement m_sbi;
};

} // namespace

CDImageCueSheet::CDImageCueSheet() = default;

CDImageCueSheet::~CDImageCueSheet()
{
  std::for_each(m_files.begin(), m_files.end(), [](TrackFile& t) { std::fclose(t.file); });
}

bool CDImageCueSheet::OpenAndParse(const char* filename, Error* error)
{
  std::FILE* fp = FileSystem::OpenSharedCFile(filename, "rb", FileSystem::FileShareMode::DenyWrite, error);
  if (!fp)
  {
    Error::AddPrefixFmt(error, "Failed to open cuesheet '{}': ", Path::GetFileName(filename));
    return false;
  }

  CueParser::File parser;
  if (!parser.Parse(fp, error))
  {
    std::fclose(fp);
    return false;
  }

  std::fclose(fp);

  m_filename = filename;

  u32 disc_lba = 0;

  // for each track..
  for (u32 track_num = 1; track_num <= CueParser::MAX_TRACK_NUMBER; track_num++)
  {
    const CueParser::Track* track = parser.GetTrack(track_num);
    if (!track)
      break;

    const std::string& track_filename = track->file;
    LBA track_start = track->start.ToLBA();

    u32 track_file_index = 0;
    for (; track_file_index < m_files.size(); track_file_index++)
    {
      const TrackFile& t = m_files[track_file_index];
      if (t.filename == track_filename)
        break;
    }
    if (track_file_index == m_files.size())
    {
      const std::string track_full_filename(
        !Path::IsAbsolute(track_filename) ? Path::BuildRelativePath(m_filename, track_filename) : track_filename);
      Error track_error;
      std::FILE* track_fp = FileSystem::OpenCFile(track_full_filename.c_str(), "rb", &track_error);
      if (!track_fp && track_file_index == 0)
      {
        // many users have bad cuesheets, or they're renamed the files without updating the cuesheet.
        // so, try searching for a bin with the same name as the cue, but only for the first referenced file.
        const std::string alternative_filename(Path::ReplaceExtension(filename, "bin"));
        track_fp = FileSystem::OpenCFile(alternative_filename.c_str(), "rb");
        if (track_fp)
        {
          WARNING_LOG("Your cue sheet references an invalid file '{}', but this was found at '{}' instead.",
                      track_filename, alternative_filename);
        }
      }

      if (!track_fp)
      {
        ERROR_LOG("Failed to open track filename '{}' (from '{}' and '{}'): {}", track_full_filename, track_filename,
                  filename, track_error.GetDescription());
        Error::SetStringFmt(error, "Failed to open track filename '{}' (from '{}' and '{}'): {}", track_full_filename,
                            track_filename, Path::GetFileName(filename), track_error.GetDescription());
        return false;
      }

      u64 data_offset = 0;
      u64 data_size = static_cast<u64>(std::max<s64>(FileSystem::FSize64(track_fp), 0));
      if (track->file_is_wave && !FindWaveAudioData(track_fp, &data_offset, &data_size, error))
      {
        Error::AddPrefixFmt(error, "Track file '{}': ", Path::GetFileName(track_filename));
        std::fclose(track_fp);
        return false;
      }

      m_files.push_back(TrackFile{track_filename, track_fp, UINT64_MAX, data_offset, data_size});
    }

    // data type determines the sector size
    const TrackMode mode = track->mode;
    const u32 track_sector_size = GetBytesPerSector(mode);

    // precompute subchannel q flags for the whole track
    SubChannelQ::Control control{};
    control.data = mode != TrackMode::Audio;
    control.audio_preemphasis = track->HasFlag(CueParser::TrackFlag::PreEmphasis);
    control.digital_copy_permitted = track->HasFlag(CueParser::TrackFlag::CopyPermitted);
    control.four_channel_audio = track->HasFlag(CueParser::TrackFlag::FourChannelAudio);

    // determine the length from the file
    LBA track_length;
    if (!track->length.has_value())
    {
      const u64 file_size = m_files[track_file_index].data_size / track_sector_size;
      if (track_start >= file_size)
      {
        ERROR_LOG("Failed to open track {} in '{}': track start is out of range ({} vs {})", track_num, filename,
                  track_start, file_size);
        Error::SetStringFmt(error, "Failed to open track {} in '{}': track start is out of range ({} vs {}))",
                            track_num, Path::GetFileName(filename), track_start, file_size);
        return false;
      }

      track_length = static_cast<LBA>(file_size - track_start);
    }
    else
    {
      track_length = track->length.value().ToLBA();
    }

    const Position* index0 = track->GetIndex(0);
    LBA pregap_frames;
    if (index0)
    {
      // index 1 is always present, so this is safe
      pregap_frames = track->GetIndex(1)->ToLBA() - index0->ToLBA();

      // Pregap/index 0 is in the file, easy.
      Index pregap_index = {};
      pregap_index.start_lba_on_disc = disc_lba;
      pregap_index.start_lba_in_track = static_cast<LBA>(-static_cast<s32>(pregap_frames));
      pregap_index.length = pregap_frames;
      pregap_index.track_number = track_num;
      pregap_index.index_number = 0;
      pregap_index.mode = mode;
      pregap_index.submode = CDImage::SubchannelMode::None;
      pregap_index.control.bits = control.bits;
      pregap_index.is_pregap = true;
      pregap_index.file_index = track_file_index;
      pregap_index.file_offset = static_cast<u64>(static_cast<s64>(track_start - pregap_frames)) * track_sector_size;
      pregap_index.file_sector_size = track_sector_size;

      m_indices.push_back(pregap_index);

      disc_lba += pregap_index.length;
    }
    else
    {
      // Two seconds pregap for track 1 is assumed if not specified.
      // Some people have broken (older) dumps where a two second pregap was implicit but not specified in the cuesheet.
      // The problem is we can't tell between a missing implicit two second pregap and a zero second pregap. Most of
      // these seem to be a single bin file for all tracks. So if this is the case, we add the two seconds in if it's
      // not specified. If this is an audio CD (likely when track 1 is not data), we don't add these pregaps, and rely
      // on the cuesheet. If we did add them, it causes issues in some games (e.g. Dancing Stage featuring DREAMS COME
      // TRUE).
      const bool is_multi_track_bin = (track_num > 1 && track_file_index == m_indices[0].file_index);
      const bool likely_audio_cd = (parser.GetTrack(1)->mode == TrackMode::Audio);

      pregap_frames = track->zero_pregap.has_value() ? track->zero_pregap->ToLBA() : 0;
      if ((track_num == 1 || is_multi_track_bin) && !track->zero_pregap.has_value() &&
          (track_num == 1 || !likely_audio_cd))
      {
        pregap_frames = 2 * FRAMES_PER_SECOND;
      }

      // create the index for the pregap
      if (pregap_frames > 0)
      {
        Index pregap_index = {};
        pregap_index.start_lba_on_disc = disc_lba;
        pregap_index.start_lba_in_track = static_cast<LBA>(-static_cast<s32>(pregap_frames));
        pregap_index.length = pregap_frames;
        pregap_index.track_number = track_num;
        pregap_index.index_number = 0;
        pregap_index.mode = mode;
        pregap_index.submode = CDImage::SubchannelMode::None;
        pregap_index.control.bits = control.bits;
        pregap_index.is_pregap = true;
        m_indices.push_back(pregap_index);

        disc_lba += pregap_index.length;
      }
    }

    // add the track itself
    m_tracks.push_back(Track{track_num, disc_lba, static_cast<u32>(m_indices.size()), track_length + pregap_frames,
                             mode, SubchannelMode::None, control});

    // how many indices in this track?
    Index last_index;
    last_index.start_lba_on_disc = disc_lba;
    last_index.start_lba_in_track = 0;
    last_index.track_number = track_num;
    last_index.index_number = 1;
    last_index.file_index = track_file_index;
    last_index.file_sector_size = track_sector_size;
    last_index.file_offset = static_cast<u64>(track_start) * track_sector_size;
    last_index.mode = mode;
    last_index.submode = CDImage::SubchannelMode::None;
    last_index.control.bits = control.bits;
    last_index.is_pregap = false;

    u32 last_index_offset = track_start;
    for (u32 index_num = 1;; index_num++)
    {
      const Position* pos = track->GetIndex(index_num);
      if (!pos)
        break;

      const u32 index_offset = pos->ToLBA();

      // add an index between the track indices
      if (index_offset > last_index_offset)
      {
        last_index.length = index_offset - last_index_offset;
        m_indices.push_back(last_index);

        disc_lba += last_index.length;
        last_index.start_lba_in_track += last_index.length;
        last_index.start_lba_on_disc = disc_lba;
        last_index.length = 0;
      }

      last_index.file_offset = index_offset * last_index.file_sector_size;
      last_index.index_number = static_cast<u32>(index_num);
      last_index_offset = index_offset;
    }

    // and the last index is added here
    const u32 track_end_index = track_start + track_length;
    DebugAssert(track_end_index >= last_index_offset);
    if (track_end_index > last_index_offset)
    {
      last_index.length = track_end_index - last_index_offset;
      m_indices.push_back(last_index);

      disc_lba += last_index.length;
    }
  }

  if (m_tracks.empty())
  {
    ERROR_LOG("File '{}' contains no tracks", filename);
    Error::SetStringFmt(error, "File '{}' contains no tracks", Path::GetFileName(filename));
    return false;
  }

  m_lba_count = disc_lba;
  AddLeadOutIndex();

  m_sbi.LoadFromImagePath(filename);

  return Seek(1, Position{0, 0, 0});
}

bool CDImageCueSheet::ReadSubChannelQ(SubChannelQ* subq, const Index& index, LBA lba_in_index)
{
  if (m_sbi.GetReplacementSubChannelQ(index.start_lba_on_disc + lba_in_index, subq))
    return true;

  return CDImage::ReadSubChannelQ(subq, index, lba_in_index);
}

bool CDImageCueSheet::LoadSubchannelReplacementFile(std::string_view path)
{
  return m_sbi.LoadFromFile(path);
}

bool CDImageCueSheet::HasNonStandardSubchannel() const
{
  return (m_sbi.GetReplacementSectorCount() > 0);
}

bool CDImageCueSheet::FindWaveAudioData(std::FILE* fp, u64* data_offset, u64* data_size, Error* error)
{
  u8 riff_header[12];
  if (FileSystem::FSeek64(fp, 0, SEEK_SET) != 0 || std::fread(riff_header, sizeof(riff_header), 1, fp) != 1 ||
      std::memcmp(riff_header, "RIFF", 4) != 0 || std::memcmp(riff_header + 8, "WAVE", 4) != 0)
  {
    Error::SetStringView(error, "Not a RIFF WAVE file.");
    return false;
  }

  bool format_ok = false;
  u64 position = sizeof(riff_header);
  for (;;)
  {
    u8 chunk_header[8];
    if (FileSystem::FSeek64(fp, static_cast<s64>(position), SEEK_SET) != 0 ||
        std::fread(chunk_header, sizeof(chunk_header), 1, fp) != 1)
    {
      Error::SetStringView(error, "WAVE file has no data chunk.");
      return false;
    }

    u32 chunk_size;
    std::memcpy(&chunk_size, chunk_header + 4, sizeof(chunk_size));
    position += sizeof(chunk_header);

    if (std::memcmp(chunk_header, "fmt ", 4) == 0)
    {
      u8 fmt[16];
      u16 format_tag, channels, bits_per_sample;
      u32 sample_rate;
      if (chunk_size < sizeof(fmt) || std::fread(fmt, sizeof(fmt), 1, fp) != 1)
      {
        Error::SetStringView(error, "WAVE format chunk is truncated.");
        return false;
      }
      std::memcpy(&format_tag, fmt + 0, sizeof(format_tag));
      std::memcpy(&channels, fmt + 2, sizeof(channels));
      std::memcpy(&sample_rate, fmt + 4, sizeof(sample_rate));
      std::memcpy(&bits_per_sample, fmt + 14, sizeof(bits_per_sample));
      if (format_tag != 1 || channels != 2 || sample_rate != 44100 || bits_per_sample != 16)
      {
        Error::SetStringFmt(error, "WAVE audio must be 16-bit stereo PCM at 44100 Hz (got format {}, {} channels, "
                                   "{} Hz, {} bits).",
                            format_tag, channels, sample_rate, bits_per_sample);
        return false;
      }
      format_ok = true;
    }
    else if (std::memcmp(chunk_header, "data", 4) == 0)
    {
      if (!format_ok)
      {
        Error::SetStringView(error, "WAVE data chunk comes before its format chunk.");
        return false;
      }

      *data_offset = position;
      *data_size = chunk_size;
      return true;
    }

    position += chunk_size + (chunk_size & 1u);
  }
}

bool CDImageCueSheet::ReadSectorFromIndex(void* buffer, const Index& index, LBA lba_in_index)
{
  DebugAssert(index.file_index < m_files.size());

  TrackFile& tf = m_files[index.file_index];
  const u64 file_position =
    tf.data_offset + index.file_offset + (static_cast<u64>(lba_in_index) * index.file_sector_size);
  if (tf.file_position != file_position)
  {
    if (FileSystem::FSeek64(tf.file, static_cast<s64>(file_position), SEEK_SET) != 0)
      return false;

    tf.file_position = file_position;
  }

  if (std::fread(buffer, index.file_sector_size, 1, tf.file) != 1)
  {
    tf.file_position = UINT64_MAX;
    return false;
  }

  tf.file_position += index.file_sector_size;
  return true;
}

s64 CDImageCueSheet::GetSizeOnDisk() const
{
  // Doesn't include the cue.. but they're tiny anyway, whatever.
  u64 size = 0;
  for (const TrackFile& tf : m_files)
    size += FileSystem::FSize64(tf.file);
  return size;
}

std::unique_ptr<CDImage> CDImage::OpenCueSheetImage(const char* filename, Error* error)
{
  std::unique_ptr<CDImageCueSheet> image = std::make_unique<CDImageCueSheet>();
  if (!image->OpenAndParse(filename, error))
    return {};

  return image;
}
