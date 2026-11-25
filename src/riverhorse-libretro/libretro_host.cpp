// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "core/achievements.h"
#include "core/bus.h"
#include "core/cheats.h"
#include "core/fullscreen_ui.h"
#include "core/game_list.h"
#include "core/gpu.h"
#include "core/host.h"
#include "core/spu.h"
#include "core/netplay.h"
#include "core/system.h"

#include "scmversion/scmversion.h"

#include "util/audio_stream.h"
#include "util/gpu_device.h"
#include "util/imgui_fullscreen.h"
#include "util/imgui_manager.h"
#include "util/input_manager.h"
#include "util/platform_misc.h"

#include "common/error.h"
#include "common/file_system.h"
#include "common/log.h"
#include "common/memory_settings_interface.h"
#include "common/path.h"
#include "common/small_string.h"
#include "common/string_util.h"

#include "libretro.h"
#include "libretro_core_options.h"
#include "libretro_input.h"
#include "libretro_resources.h"
#include "libretro_rumble.h"
#ifdef ENABLE_OPENGL
#include "libretro_video_opengl.h"
#endif
#ifdef ENABLE_VULKAN
#include "libretro_video_vulkan.h"
#endif

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <vector>

Log_SetChannel(LibretroHost);

namespace LibretroHost {
static bool InitializeConfig();
static void LogCallback(void* user, const char* channel, const char* function, LOGLEVEL level,
                        std::string_view message);
static void SendVideoFrame();
static void SendAudio();
static retro_game_geometry GetGeometry();
static void UpdateGeometry();
static void ApplyCoreOptions();
static void RegisterDiscControl();
static void UpdateCheats();
static bool BootGame();
static bool SetupHardwareOutput();
static void OnContextReset();
static void OnContextDestroy();
static void SendHardwareFrame();
static void DestroyHardwareFrameResources();
} // namespace LibretroHost

static retro_environment_t s_environment;
static retro_video_refresh_t s_video_refresh;
static retro_audio_sample_batch_t s_audio_sample_batch;
static retro_log_printf_t s_log_printf;

static std::unique_ptr<MemorySettingsInterface> s_base_settings_interface;
static std::string s_system_directory;
static std::string s_save_directory;

static std::vector<u32> s_video_buffer;
static std::vector<u32> s_xrgb_buffer;
static u32 s_video_width = 320;
static u32 s_video_height = 240;
static float s_video_aspect_ratio = 4.0f / 3.0f;
static bool s_can_dupe_frames = false;

static std::vector<AudioStream::SampleType> s_audio_buffer;

static std::string s_loaded_renderer;

static bool s_tray_open = false;
static u32 s_pending_disc_index = 0;
static u32 s_initial_disc_index = 0;

enum class VideoOutput
{
  Readback,
  OpenGL,
  Vulkan,
};

static VideoOutput s_video_output = VideoOutput::Readback;
static std::string s_game_path;
static std::vector<u8> s_context_lost_state;

static std::vector<std::string> s_cheat_codes;
static std::vector<bool> s_cheat_enabled;

static constexpr u32 AUDIO_BUFFER_MS = 200;
static constexpr u32 POINTER_WINDOW_WIDTH = 640;
static constexpr u32 POINTER_WINDOW_HEIGHT = 480;

void LibretroHost::LogCallback(void* user, const char* channel, const char* function, LOGLEVEL level,
                               std::string_view message)
{
  if (!s_log_printf)
    return;

  retro_log_level retro_level;
  switch (level)
  {
    case LOGLEVEL_ERROR:
      retro_level = RETRO_LOG_ERROR;
      break;
    case LOGLEVEL_WARNING:
      retro_level = RETRO_LOG_WARN;
      break;
    case LOGLEVEL_INFO:
      retro_level = RETRO_LOG_INFO;
      break;
    default:
      retro_level = RETRO_LOG_DEBUG;
      break;
  }

  s_log_printf(retro_level, "[%s] %.*s\n", channel, static_cast<int>(message.size()), message.data());
}

bool LibretroHost::InitializeConfig()
{
  const char* system_directory = nullptr;
  if (!s_environment(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_directory) || !system_directory)
  {
    ERROR_LOG("The frontend did not provide a system directory.");
    return false;
  }

  const char* save_directory = nullptr;
  s_system_directory = system_directory;
  s_save_directory = (s_environment(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_directory) && save_directory) ?
                       std::string(save_directory) :
                       s_system_directory;

  EmuFolders::AppRoot = Path::Combine(s_system_directory, "riverhorse");
  EmuFolders::DataRoot = EmuFolders::AppRoot;

  s_base_settings_interface = std::make_unique<MemorySettingsInterface>();
  Host::Internal::SetBaseSettingsLayer(s_base_settings_interface.get());

  SettingsInterface& si = *s_base_settings_interface;
  g_settings.Save(si, false);
  si.SetStringValue("BIOS", "SearchDirectory", s_system_directory.c_str());
  si.SetStringValue("MemoryCards", "Directory", s_save_directory.c_str());
  si.SetStringValue("MemoryCards", "Card1Type", Settings::GetMemoryCardTypeName(MemoryCardType::PerGameTitle));
  si.SetStringValue("Audio", "Backend", AudioStream::GetBackendName(AudioBackend::Null));
  si.SetUIntValue("Audio", "BufferMS", AUDIO_BUFFER_MS);
  si.SetStringValue("Audio", "StretchMode", AudioStream::GetStretchModeName(AudioStretchMode::Off));
  si.SetFloatValue("Main", "EmulationSpeed", 0.0f);
  si.SetBoolValue("Main", "RewindEnable", false);
  si.SetIntValue("Main", "RunaheadFrameCount", 0);
  si.SetBoolValue("GPU", "UseHardwareThread", false);
  si.SetBoolValue("Main", "PauseOnFocusLoss", false);
  si.SetBoolValue("Main", "SaveStateOnExit", false);
  si.SetBoolValue("Main", "InhibitScreensaver", false);
  si.SetBoolValue("Display", "ShowOSDMessages", false);
  si.SetBoolValue("Logging", "LogToConsole", false);
  si.SetBoolValue("Logging", "LogToFile", false);

  for (u32 i = 0; i < static_cast<u32>(InputSourceType::Count); i++)
    si.SetBoolValue("InputSources", InputManager::InputSourceToString(static_cast<InputSourceType>(i)), false);

  LibretroCoreOptions::Apply(s_environment, si);
  LibretroInput::ApplyPortDevices(si);
  s_loaded_renderer = si.GetStringValue("GPU", "Renderer");

  EmuFolders::LoadConfig(si);
  FileSystem::EnsureDirectoryExists(EmuFolders::DataRoot.c_str(), true);
  EmuFolders::EnsureFoldersExist();

  ImGuiManager::SetFontPathAndRange(std::string(), {0x0020, 0x00FF, 0, 0});
  return true;
}

retro_game_geometry LibretroHost::GetGeometry()
{
  retro_game_geometry geometry = {};
  geometry.base_width = s_video_width;
  geometry.base_height = s_video_height;
  const u32 scale = (g_settings.gpu_renderer == GPURenderer::Software) ? 1u : std::max<u32>(g_settings.gpu_resolution_scale, 1);
  geometry.max_width = VRAM_WIDTH * scale;
  geometry.max_height = VRAM_HEIGHT * scale;
  geometry.aspect_ratio = s_video_aspect_ratio;
  return geometry;
}

void LibretroHost::UpdateGeometry()
{
  retro_game_geometry geometry = GetGeometry();
  s_environment(RETRO_ENVIRONMENT_SET_GEOMETRY, &geometry);
}

void LibretroHost::ApplyCoreOptions()
{
  const u32 old_scale = g_settings.gpu_resolution_scale;

  SettingsInterface& si = *s_base_settings_interface;
  LibretroCoreOptions::Apply(s_environment, si);
  LibretroInput::ApplyPortDevices(si);
  si.SetStringValue("GPU", "Renderer", s_loaded_renderer.c_str());
  System::ApplySettings(false);

  if (g_settings.gpu_resolution_scale != old_scale)
  {
    retro_system_av_info av_info;
    retro_get_system_av_info(&av_info);
    s_environment(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av_info);
  }
}

void LibretroHost::RegisterDiscControl()
{
  static constexpr retro_disk_control_ext_callback callbacks = {
    .set_eject_state = [](bool ejected) {
      if (!System::IsValid())
        return false;

      if (ejected)
      {
        s_pending_disc_index = System::HasMediaSubImages() ? System::GetMediaSubImageIndex() : 0;
      }
      else if (System::HasMediaSubImages() && s_pending_disc_index != System::GetMediaSubImageIndex() &&
               !System::SwitchMediaSubImage(s_pending_disc_index))
      {
        return false;
      }

      s_tray_open = ejected;
      return true;
    },
    .get_eject_state = []() { return s_tray_open; },
    .get_image_index = []() -> unsigned {
      if (s_tray_open)
        return s_pending_disc_index;
      return (System::IsValid() && System::HasMediaSubImages()) ? System::GetMediaSubImageIndex() : 0;
    },
    .set_image_index = [](unsigned index) {
      if (!s_tray_open || !System::IsValid() || index >= std::max(System::GetMediaSubImageCount(), 1u))
        return false;
      s_pending_disc_index = index;
      return true;
    },
    .get_num_images = []() -> unsigned {
      if (!System::IsValid())
        return 0;
      return System::HasMediaSubImages() ? System::GetMediaSubImageCount() : (System::HasMedia() ? 1u : 0u);
    },
    .replace_image_index = [](unsigned index, const retro_game_info* info) { return false; },
    .add_image_index = []() { return false; },
    .set_initial_image = [](unsigned index, const char* path) {
      s_initial_disc_index = index;
      return true;
    },
    .get_image_path = [](unsigned index, char* path, size_t len) {
      if (s_game_path.empty())
        return false;
      StringUtil::Strlcpy(path, s_game_path, len);
      return true;
    },
    .get_image_label = [](unsigned index, char* label, size_t len) {
      if (!System::IsValid() || !System::HasMediaSubImages() || index >= System::GetMediaSubImageCount())
        return false;
      StringUtil::Strlcpy(label, System::GetMediaSubImageTitle(index), len);
      return true;
    },
  };
  s_environment(RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE, const_cast<retro_disk_control_ext_callback*>(&callbacks));
}

void LibretroHost::UpdateCheats()
{
  if (!System::IsValid())
    return;

  std::unique_ptr<CheatList> list = std::make_unique<CheatList>();
  for (size_t i = 0; i < s_cheat_codes.size(); i++)
  {
    if (!s_cheat_enabled[i] || s_cheat_codes[i].empty())
      continue;

    std::string instructions = s_cheat_codes[i];
    std::replace(instructions.begin(), instructions.end(), '+', '\n');

    CheatCode code;
    code.description = fmt::format("Cheat {}", i + 1);
    code.enabled = true;
    if (!code.SetInstructionsFromString(instructions))
    {
      WARNING_LOG("Ignoring cheat {}: '{}' is not a GameShark code.", i + 1, s_cheat_codes[i]);
      continue;
    }

    list->AddCode(std::move(code));
  }

  System::SetCheatList(list->GetCodeCount() > 0 ? std::move(list) : nullptr);
}

bool LibretroHost::BootGame()
{
  Error error;
  SystemBootParameters parameters(s_game_path);
  parameters.media_playlist_index = s_initial_disc_index;
  if (!System::BootSystem(std::move(parameters), &error))
  {
    ERROR_LOG("Failed to boot '{}': {}", s_game_path, error.GetDescription());
    return false;
  }

  return true;
}

bool LibretroHost::SetupHardwareOutput()
{
#ifdef ENABLE_VULKAN
  if (LibretroVulkan::Setup(s_environment, &OnContextReset, &OnContextDestroy))
  {
    s_video_output = VideoOutput::Vulkan;
    s_loaded_renderer = Settings::GetRendererName(GPURenderer::HardwareVulkan);
    s_base_settings_interface->SetStringValue("GPU", "Renderer", s_loaded_renderer.c_str());
    return true;
  }
#endif
#ifdef ENABLE_OPENGL
  if (LibretroOpenGL::Setup(s_environment, &OnContextReset, &OnContextDestroy))
  {
    s_video_output = VideoOutput::OpenGL;
    s_loaded_renderer = Settings::GetRendererName(GPURenderer::HardwareOpenGL);
    s_base_settings_interface->SetStringValue("GPU", "Renderer", s_loaded_renderer.c_str());
    return true;
  }
#endif
  return false;
}

void LibretroHost::OnContextReset()
{
#ifdef ENABLE_VULKAN
  if (s_video_output == VideoOutput::Vulkan)
    LibretroVulkan::OnContextReset(s_environment);
#endif

  if (!BootGame())
    return;

  if (!s_context_lost_state.empty())
  {
    Error error;
    if (!System::LoadStateFromMemory(s_context_lost_state, &error))
      ERROR_LOG("Failed to restore state after the video context was recreated: {}", error.GetDescription());
    s_context_lost_state.clear();
  }
}

void LibretroHost::OnContextDestroy()
{
  if (!System::IsValid())
    return;

  Error error;
  s_context_lost_state.resize(System::GetMaxSaveStateSize());
  if (!System::SaveStateToMemory(s_context_lost_state, &error))
  {
    ERROR_LOG("Failed to keep state while the video context is recreated: {}", error.GetDescription());
    s_context_lost_state.clear();
  }

  DestroyHardwareFrameResources();
  System::ShutdownSystem(false);
  System::ExecuteFrame();
  if (g_gpu_device)
    Host::ReleaseGPUDevice();
}

void LibretroHost::DestroyHardwareFrameResources()
{
#ifdef ENABLE_OPENGL
  if (s_video_output == VideoOutput::OpenGL)
    LibretroOpenGL::DestroyFrameResources();
#endif
#ifdef ENABLE_VULKAN
  if (s_video_output == VideoOutput::Vulkan)
    LibretroVulkan::DestroyFrameResources();
#endif
}

void LibretroHost::SendHardwareFrame()
{
  u32 width = 0, height = 0;
  bool rendered = false;
#ifdef ENABLE_OPENGL
  if (s_video_output == VideoOutput::OpenGL)
    rendered = LibretroOpenGL::RenderFrame(&width, &height);
#endif
#ifdef ENABLE_VULKAN
  if (s_video_output == VideoOutput::Vulkan)
    rendered = LibretroVulkan::RenderFrame(&width, &height);
#endif
  if (!rendered)
  {
    if (s_can_dupe_frames)
      s_video_refresh(nullptr, s_video_width, s_video_height, 0);
    return;
  }

  const float aspect_ratio = g_gpu->ComputeDisplayAspectRatio();
  if (width != s_video_width || height != s_video_height || aspect_ratio != s_video_aspect_ratio)
  {
    s_video_width = width;
    s_video_height = height;
    s_video_aspect_ratio = aspect_ratio;
    UpdateGeometry();
  }

  s_video_refresh(RETRO_HW_FRAME_BUFFER_VALID, width, height, 0);
}

void LibretroHost::SendVideoFrame()
{
  u32 stride, width, height;
  GPUTexture::Format format;
  if (!g_gpu->ReadDisplayTexture(&s_video_buffer, &stride, &width, &height, &format) ||
      !GPUTexture::ConvertTextureDataToRGBA8(width, height, s_video_buffer, stride, format))
  {
    if (s_can_dupe_frames)
      s_video_refresh(nullptr, s_video_width, s_video_height, 0);
    return;
  }

  s_xrgb_buffer.resize(width * height);
  for (u32 y = 0; y < height; y++)
  {
    const u32* src = s_video_buffer.data() + (y * (stride / sizeof(u32)));
    u32* dst = s_xrgb_buffer.data() + (y * width);
    for (u32 x = 0; x < width; x++)
    {
      const u32 rgba = src[x];
      dst[x] = ((rgba & 0xFFu) << 16) | (rgba & 0xFF00u) | ((rgba >> 16) & 0xFFu);
    }
  }

  const float aspect_ratio = g_gpu->ComputeDisplayAspectRatio();
  if (width != s_video_width || height != s_video_height || aspect_ratio != s_video_aspect_ratio)
  {
    s_video_width = width;
    s_video_height = height;
    s_video_aspect_ratio = aspect_ratio;
    UpdateGeometry();
  }

  s_video_refresh(s_xrgb_buffer.data(), width, height, width * sizeof(u32));
}

void LibretroHost::SendAudio()
{
  AudioStream* stream = SPU::GetOutputStream();
  const u32 max_frames = AudioStream::GetBufferSizeForMS(SPU::SAMPLE_RATE, AUDIO_BUFFER_MS);
  s_audio_buffer.resize(max_frames * AudioStream::NUM_INPUT_CHANNELS);

  const u32 num_frames = stream->ReadBufferedFrames(s_audio_buffer.data(), max_frames);
  u32 sent = 0;
  while (sent < num_frames)
    sent += static_cast<u32>(s_audio_sample_batch(&s_audio_buffer[sent * AudioStream::NUM_INPUT_CHANNELS],
                                                  num_frames - sent));
}

RETRO_API void retro_set_environment(retro_environment_t cb)
{
  s_environment = cb;

  retro_log_callback log = {};
  if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
    s_log_printf = log.log;

  bool no_game = false;
  cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);

  LibretroCoreOptions::Register(cb);
  LibretroInput::RegisterControllers(s_environment);
  LibretroHost::RegisterDiscControl();
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb)
{
  s_video_refresh = cb;
}

RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb)
{
}

RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
  s_audio_sample_batch = cb;
}

RETRO_API void retro_set_input_poll(retro_input_poll_t cb)
{
  LibretroInput::SetPollCallback(cb);
}

RETRO_API void retro_set_input_state(retro_input_state_t cb)
{
  LibretroInput::SetStateCallback(cb);
}

RETRO_API unsigned retro_api_version(void)
{
  return RETRO_API_VERSION;
}

RETRO_API void retro_get_system_info(retro_system_info* info)
{
  std::memset(info, 0, sizeof(*info));
  info->library_name = "Riverhorse";
  info->library_version = g_scm_tag_str;
  info->valid_extensions = "cue|bin|img|iso|chd|ecm|mds|ccd|pbp|m3u|exe|psexe|psf";
  info->need_fullpath = true;
  info->block_extract = false;
}

RETRO_API void retro_get_system_av_info(retro_system_av_info* info)
{
  std::memset(info, 0, sizeof(*info));
  info->geometry = LibretroHost::GetGeometry();
  info->timing.fps = System::IsValid() ? System::GetThrottleFrequency() : 60.0;
  info->timing.sample_rate = SPU::SAMPLE_RATE;
}

RETRO_API void retro_init(void)
{
  Log::RegisterCallback(&LibretroHost::LogCallback, nullptr);
  Log::SetLogLevel(LOGLEVEL_INFO);

  s_can_dupe_frames = false;
  s_environment(RETRO_ENVIRONMENT_GET_CAN_DUPE, &s_can_dupe_frames);
  LibretroRumble::Initialize(s_environment);
}

RETRO_API void retro_deinit(void)
{
  LibretroRumble::Shutdown();
  Log::UnregisterCallback(&LibretroHost::LogCallback, nullptr);
}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device)
{
  if (!LibretroInput::SetPortDevice(port, device))
    return;

  if (System::IsValid())
  {
    LibretroInput::ApplyPortDevices(*s_base_settings_interface);
    System::ApplySettings(false);
  }
}

RETRO_API void retro_reset(void)
{
  System::ResetSystem();
}

RETRO_API void retro_run(void)
{
  if (LibretroCoreOptions::HaveChanged(s_environment))
    LibretroHost::ApplyCoreOptions();

  if (!System::IsValid())
    return;

  LibretroInput::Update();
  System::ExecuteFrame();
  if (s_video_output == VideoOutput::Readback)
    LibretroHost::SendVideoFrame();
  else
    LibretroHost::SendHardwareFrame();
  LibretroHost::SendAudio();
}

RETRO_API size_t retro_serialize_size(void)
{
  return System::GetMaxSaveStateSize();
}

RETRO_API bool retro_serialize(void* data, size_t size)
{
  Error error;
  if (!System::SaveStateToMemory(std::span<u8>(static_cast<u8*>(data), size), &error))
  {
    ERROR_LOG("Failed to save state: {}", error.GetDescription());
    return false;
  }

  return true;
}

RETRO_API bool retro_unserialize(const void* data, size_t size)
{
  Error error;
  if (!System::LoadStateFromMemory(std::span<const u8>(static_cast<const u8*>(data), size), &error))
  {
    ERROR_LOG("Failed to load state: {}", error.GetDescription());
    return false;
  }

  return true;
}

RETRO_API void retro_cheat_reset(void)
{
  s_cheat_codes.clear();
  s_cheat_enabled.clear();
  LibretroHost::UpdateCheats();
}

RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char* code)
{
  if (index >= s_cheat_codes.size())
  {
    s_cheat_codes.resize(index + 1);
    s_cheat_enabled.resize(index + 1);
  }

  s_cheat_codes[index] = code ? code : "";
  s_cheat_enabled[index] = enabled;
  LibretroHost::UpdateCheats();
}

RETRO_API bool retro_load_game(const retro_game_info* game)
{
  if (!game || !game->path)
    return false;

  retro_pixel_format pixel_format = RETRO_PIXEL_FORMAT_XRGB8888;
  if (!s_environment(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &pixel_format))
  {
    ERROR_LOG("The frontend does not support XRGB8888 output.");
    return false;
  }

  if (!LibretroHost::InitializeConfig())
    return false;

  Error error;
  if (!System::Internal::PerformEarlyHardwareChecks(&error) || !System::Internal::ProcessStartup(&error) ||
      !System::Internal::CPUThreadInitialize(&error))
  {
    ERROR_LOG("Failed to start up: {}", error.GetDescription());
    return false;
  }

  s_game_path = game->path;
  s_video_output = VideoOutput::Readback;
  if (s_loaded_renderer != Settings::GetRendererName(GPURenderer::Software))
  {
    if (LibretroHost::SetupHardwareOutput())
      return true;

    WARNING_LOG("The frontend has no usable hardware context, using the software renderer.");
    s_loaded_renderer = Settings::GetRendererName(GPURenderer::Software);
    s_base_settings_interface->SetStringValue("GPU", "Renderer", s_loaded_renderer.c_str());
  }

#ifdef ENABLE_VULKAN
  const GPUDevice::AdapterInfoList vulkan_adapters = GPUDevice::GetAdapterListForAPI(RenderAPI::Vulkan);
  if (std::any_of(vulkan_adapters.begin(), vulkan_adapters.end(),
                  [](const GPUDevice::AdapterInfo& adapter) { return !GPUDevice::IsSoftwareAdapter(adapter.name); }))
  {
    GPUDevice::SetPreferredAPI(RenderAPI::Vulkan);
  }
  else
  {
    WARNING_LOG("No hardware Vulkan device found; the core's OpenGL context may conflict with an OpenGL frontend.");
  }
#endif

  if (!LibretroHost::BootGame())
  {
    System::Internal::CPUThreadShutdown();
    System::Internal::ProcessShutdown();
    return false;
  }

  return true;
}

RETRO_API bool retro_load_game_special(unsigned game_type, const retro_game_info* info, size_t num_info)
{
  return false;
}

RETRO_API void retro_unload_game(void)
{
  if (g_gpu_device)
    LibretroHost::DestroyHardwareFrameResources();
  s_context_lost_state.clear();

  System::ShutdownSystem(false);
  System::ExecuteFrame();
  if (g_gpu_device)
    Host::ReleaseGPUDevice();
#ifdef ENABLE_OPENGL
  if (s_video_output == VideoOutput::OpenGL)
    LibretroOpenGL::Shutdown();
#endif
#ifdef ENABLE_VULKAN
  if (s_video_output == VideoOutput::Vulkan)
    LibretroVulkan::Shutdown();
#endif
  System::Internal::CPUThreadShutdown();
  System::Internal::ProcessShutdown();
  s_base_settings_interface.reset();
}

RETRO_API unsigned retro_get_region(void)
{
  return (System::IsValid() && System::IsPALRegion()) ? RETRO_REGION_PAL : RETRO_REGION_NTSC;
}

RETRO_API void* retro_get_memory_data(unsigned id)
{
  return (id == RETRO_MEMORY_SYSTEM_RAM && System::IsValid()) ? Bus::g_ram : nullptr;
}

RETRO_API size_t retro_get_memory_size(unsigned id)
{
  return (id == RETRO_MEMORY_SYSTEM_RAM && System::IsValid()) ? Bus::RAM_2MB_SIZE : 0;
}

void Host::ReportFatalError(std::string_view title, std::string_view message)
{
  ERROR_LOG("{}: {}", title, message);
  System::ShutdownSystem(false);
}

void Host::ReportErrorAsync(std::string_view title, std::string_view message)
{
  ERROR_LOG("{}: {}", title, message);
}

bool Host::ConfirmMessage(std::string_view title, std::string_view message)
{
  WARNING_LOG("{}: {}", title, message);
  return true;
}

void Host::ReportDebuggerMessage(std::string_view message)
{
  INFO_LOG("{}", message);
}

std::span<const std::pair<const char*, const char*>> Host::GetAvailableLanguageList()
{
  return {};
}

bool Host::ChangeLanguage(const char* new_language)
{
  return false;
}

s32 Host::Internal::GetTranslatedStringImpl(std::string_view context, std::string_view msg, char* tbuf,
                                            size_t tbuf_space)
{
  if (msg.size() > tbuf_space)
    return -1;
  else if (msg.empty())
    return 0;

  std::memcpy(tbuf, msg.data(), msg.size());
  return static_cast<s32>(msg.size());
}

std::string Host::TranslatePluralToString(const char* context, const char* msg, const char* disambiguation, int count)
{
  const TinyString count_str = TinyString::from_format("{}", count);

  std::string ret(msg);
  for (std::string::size_type pos = ret.find("%n"); pos != std::string::npos; pos = ret.find("%n"))
    ret.replace(pos, 2, count_str.view());

  return ret;
}

void Host::LoadSettings(SettingsInterface& si, std::unique_lock<std::mutex>& lock)
{
}

void Host::CheckForSettingsChanges(const Settings& old_settings)
{
}

void Host::CommitBaseSettingChanges()
{
}

bool Host::ResourceFileExists(std::string_view filename, bool allow_override)
{
  return LibretroResources::Find(filename).has_value();
}

std::optional<std::vector<u8>> Host::ReadResourceFile(std::string_view filename, bool allow_override)
{
  const std::optional<LibretroResources::File> file = LibretroResources::Find(filename);
  if (!file.has_value())
  {
    ERROR_LOG("Resource file '{}' is not built into the core", filename);
    return std::nullopt;
  }

  return std::vector<u8>(file->data.begin(), file->data.end());
}

std::optional<std::string> Host::ReadResourceFileToString(std::string_view filename, bool allow_override)
{
  const std::optional<LibretroResources::File> file = LibretroResources::Find(filename);
  if (!file.has_value())
  {
    ERROR_LOG("Resource file '{}' is not built into the core", filename);
    return std::nullopt;
  }

  return std::string(reinterpret_cast<const char*>(file->data.data()), file->data.size());
}

std::optional<std::time_t> Host::GetResourceFileTimestamp(std::string_view filename, bool allow_override)
{
  const std::optional<LibretroResources::File> file = LibretroResources::Find(filename);
  if (!file.has_value())
    return std::nullopt;

  return file->timestamp;
}

void Host::OnSystemStarting()
{
}

void Host::OnSystemStarted()
{
}

void Host::OnSystemDestroyed()
{
}

void Host::OnSystemPaused()
{
}

void Host::OnSystemResumed()
{
}

void Host::OnIdleStateChanged()
{
}

void Host::OnPerformanceCountersUpdated()
{
}

void Host::OnGameChanged(const std::string& disc_path, const std::string& game_serial, const std::string& game_name)
{
}

void Host::OnMediaCaptureStarted()
{
}

void Host::OnMediaCaptureStopped()
{
}

void Host::PumpMessagesOnCPUThread()
{
  System::InterruptExecution();
}

void Host::RunOnCPUThread(std::function<void()> function, bool block)
{
  function();
}

void Host::RequestResizeHostDisplay(s32 width, s32 height)
{
}

void Host::RequestExitApplication(bool save_state_if_running)
{
}

void Host::RequestExitBigPicture()
{
}

void Host::RequestSystemShutdown(bool allow_confirm, bool save_state)
{
}

bool Host::IsFullscreen()
{
  return false;
}

void Host::SetFullscreen(bool enabled)
{
}

std::optional<WindowInfo> Host::AcquireRenderWindow(bool recreate_window)
{
  WindowInfo wi;
  wi.SetSurfaceless();
  wi.surface_width = POINTER_WINDOW_WIDTH;
  wi.surface_height = POINTER_WINDOW_HEIGHT;
  return wi;
}

void Host::ReleaseRenderWindow()
{
}

void Host::FrameDone()
{
}

void Host::OnNetplaySessionOpened(bool is_host)
{
}

void Host::OnNetplaySessionClosed()
{
}

void Host::OpenURL(std::string_view url)
{
}

bool Host::CopyTextToClipboard(std::string_view text)
{
  return false;
}

void Host::SetMouseMode(bool relative, bool hide_cursor)
{
}

void Host::OnAchievementsLoginRequested(Achievements::LoginRequestReason reason)
{
}

void Host::OnAchievementsLoginSuccess(const char* username, u32 points, u32 sc_points, u32 unread_messages)
{
}

void Host::OnAchievementsRefreshed()
{
}

void Host::OnAchievementsHardcoreModeChanged(bool enabled)
{
}

void Host::OnCoverDownloaderOpenRequested()
{
}

bool Host::ShouldPreferHostFileSelector()
{
  return false;
}

void Host::OpenHostFileSelectorAsync(std::string_view title, bool select_directory, FileSelectorCallback callback,
                                     FileSelectorFilters filters, std::string_view initial_directory)
{
  callback(std::string());
}

std::optional<u32> InputManager::ConvertHostKeyboardStringToCode(std::string_view str)
{
  return std::nullopt;
}

std::optional<std::string> InputManager::ConvertHostKeyboardCodeToString(u32 code)
{
  return std::nullopt;
}

const char* InputManager::ConvertHostKeyboardCodeToIcon(u32 code)
{
  return nullptr;
}

void Host::AddFixedInputBindings(SettingsInterface& si)
{
  LibretroRumble::AddBindings();
}

void Host::OnInputDeviceConnected(std::string_view identifier, std::string_view device_name)
{
}

void Host::OnInputDeviceDisconnected(InputBindingKey key, std::string_view identifier)
{
}

std::optional<WindowInfo> Host::GetTopLevelWindowInfo()
{
  return std::nullopt;
}

void Host::RefreshGameListAsync(bool invalidate_cache)
{
}

void Host::CancelGameListRefresh()
{
}

BEGIN_HOTKEY_LIST(g_host_hotkeys)
END_HOTKEY_LIST()
