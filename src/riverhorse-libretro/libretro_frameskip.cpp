// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "libretro_frameskip.h"

#include "core/types.h"

#include "common/timer.h"

namespace LibretroFrameSkip {

static constexpr u32 MAX_CONSECUTIVE_SKIPPED_FRAMES = 3;
static constexpr float DEFAULT_DISPLAY_REFRESH_RATE = 60.0f;

static retro_environment_t s_environment;
static bool s_can_dupe_frames = false;
static bool s_automatic = false;
static bool s_audio_underrun_likely = false;
static u32 s_consecutive_skipped_frames = 0;
static Common::Timer::Value s_last_sent_frame_time = 0;

static void RETRO_CALLCONV OnAudioBufferStatus(bool active, unsigned occupancy, bool underrun_likely)
{
  s_audio_underrun_likely = active && underrun_likely;
}

static bool IsFastForwarding()
{
  bool fast_forwarding = false;
  return s_environment(RETRO_ENVIRONMENT_GET_FASTFORWARDING, &fast_forwarding) && fast_forwarding;
}

static bool SentFrameWithinDisplayRefresh()
{
  float refresh_rate = DEFAULT_DISPLAY_REFRESH_RATE;
  if (!s_environment(RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE, &refresh_rate) || refresh_rate <= 0.0f)
    refresh_rate = DEFAULT_DISPLAY_REFRESH_RATE;

  const double since_last_frame =
    Common::Timer::ConvertValueToSeconds(Common::Timer::GetCurrentValue() - s_last_sent_frame_time);
  return since_last_frame < (1.0 / refresh_rate);
}

void Initialize(retro_environment_t environment, bool can_dupe_frames)
{
  s_environment = environment;
  s_can_dupe_frames = can_dupe_frames;
  s_consecutive_skipped_frames = 0;
  s_last_sent_frame_time = 0;
}

void Shutdown()
{
  SetAutomatic(false);
}

void SetAutomatic(bool enabled)
{
  if (s_automatic == enabled)
    return;

  retro_audio_buffer_status_callback callback = {enabled ? &OnAudioBufferStatus : nullptr};
  s_automatic = enabled && s_environment(RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK, &callback);
  s_audio_underrun_likely = false;
}

bool ShouldSkipFrame()
{
  bool skip = false;
  if (s_can_dupe_frames)
  {
    if (IsFastForwarding())
      skip = SentFrameWithinDisplayRefresh();
    else
      skip = s_automatic && s_audio_underrun_likely && s_consecutive_skipped_frames < MAX_CONSECUTIVE_SKIPPED_FRAMES;
  }

  if (skip)
  {
    s_consecutive_skipped_frames++;
    return true;
  }

  s_consecutive_skipped_frames = 0;
  s_last_sent_frame_time = Common::Timer::GetCurrentValue();
  return false;
}

} // namespace LibretroFrameSkip
