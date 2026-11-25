// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "libretro_core_options.h"

#include "common/settings_interface.h"

#include <cstring>
#include <vector>

namespace LibretroCoreOptions {
namespace {

struct OptionValue
{
  const char* value;
  const char* label;
};

struct Option
{
  const char* category;
  const char* key;
  const char* description;
  const char* info;
  const char* section;
  const char* setting;
  std::vector<OptionValue> values;
  const char* default_value;
  void (*apply)(retro_environment_t environment, SettingsInterface& si, const char* value) = nullptr;
};

} // namespace

static void ApplyOverclock(retro_environment_t environment, SettingsInterface& si, const char* percent);
static void ApplyForceAnalog(retro_environment_t environment, SettingsInterface& si, const char* value);
static void ApplyCropMode(retro_environment_t environment, SettingsInterface& si, const char* value);
static void ApplyMemoryCard2(retro_environment_t environment, SettingsInterface& si, const char* value);

static const std::vector<OptionValue> s_on_off = {{"true", "Enabled"}, {"false", "Disabled"}};
static std::vector<CheatOption> s_cheat_options;

static const std::vector<Option> s_options = {
  {"video",
   "riverhorse_renderer",
   "Renderer (restart)",
   "Hardware renders with the frontend's OpenGL or Vulkan driver and allows upscaling; Software matches the console "
   "most closely. Takes effect when content is loaded.",
   "GPU",
   "Renderer",
   {{"Automatic", "Hardware"}, {"Software", "Software"}},
   "Automatic"},
  {"video",
   "riverhorse_resolution_scale",
   "Internal Resolution",
   "Renders 3D at a multiple of the console's resolution. Hardware renderer only.",
   "GPU",
   "ResolutionScale",
   {{"1", "1x (Native)"},
    {"2", "2x"},
    {"3", "3x (1080p)"},
    {"4", "4x"},
    {"5", "5x (1440p)"},
    {"6", "6x"},
    {"8", "8x (4K)"}},
   "1"},
  {"video",
   "riverhorse_texture_filter",
   "Texture Filtering",
   "Smooths textures. Hardware renderer only.",
   "GPU",
   "TextureFilter",
   {{"Nearest", "Nearest-Neighbor"},
    {"Bilinear", "Bilinear"},
    {"BilinearBinAlpha", "Bilinear (No Edge Blending)"},
    {"JINC2", "JINC2"},
    {"xBR", "xBR"},
    {"MMPX", "MMPX (Pixel Art)"},
    {"Cubic", "Cubic"}},
   "Nearest"},
  {"video",
   "riverhorse_frameskip",
   "Frameskip",
   "Auto leaves frames out when the device can't keep up, so sound doesn't crackle. Needs the frontend's Audio "
   "Buffer Status support.",
   "Libretro",
   "FrameSkip",
   {{"Disabled", "Disabled"}, {"Auto", "Auto"}},
   "Disabled"},
  {"video", "riverhorse_osd_messages", "On-Screen Messages",
   "Shows notices such as disc changes and applied patches through the frontend.", "Libretro", "OSDMessages", s_on_off,
   "true"},
  {"video", "riverhorse_true_color", "True Color (24-Bit)", "Renders with full colour instead of 16-bit dithering.",
   "GPU", "TrueColor", s_on_off, "false"},
  {"video", "riverhorse_scaled_dithering", "Scaled Dithering",
   "Scales the dither pattern with the internal resolution.", "GPU", "ScaledDithering", s_on_off, "true"},
  {"video", "riverhorse_widescreen_hack", "Widescreen Hack",
   "Widens the 3D view of most games. Use with a 16:9 aspect ratio.", "GPU", "WidescreenHack", s_on_off, "false"},
  {"pgxp", "riverhorse_pgxp", "PGXP Geometry Correction", "Reduces polygon wobble. Hardware renderer only.", "GPU",
   "PGXPEnable", s_on_off, "false"},
  {"pgxp", "riverhorse_pgxp_texture_correction", "PGXP Texture Correction", "Corrects texture warping. Needs PGXP.",
   "GPU", "PGXPTextureCorrection", s_on_off, "true"},
  {"pgxp", "riverhorse_pgxp_culling", "PGXP Culling Correction",
   "Uses precise positions for back-face culling. Needs PGXP.", "GPU", "PGXPCulling", s_on_off, "true"},
  {"pgxp", "riverhorse_pgxp_depth_buffer", "PGXP Depth Buffer",
   "Sorts geometry with a depth buffer. Needs PGXP; slower.", "GPU", "PGXPDepthBuffer", s_on_off, "false"},
  {"pgxp", "riverhorse_pgxp_cpu", "PGXP CPU Mode", "Tracks precision through all CPU instructions. Needs PGXP; slow.",
   "GPU", "PGXPCPU", s_on_off, "false"},
  {"pgxp", "riverhorse_pgxp_vertex_cache", "PGXP Vertex Cache",
   "Finds precise positions for geometry the game copies through memory. Fixes PGXP in some games, breaks it in "
   "others. Needs PGXP.",
   "GPU", "PGXPVertexCache", s_on_off, "false"},
  {"pgxp",
   "riverhorse_pgxp_tolerance",
   "PGXP Geometry Tolerance",
   "Falls back to the console's positions when the precise ones are this many pixels away, which removes stretched "
   "polygons in some games. Needs PGXP.",
   "GPU",
   "PGXPTolerance",
   {{"-1", "Disabled"}, {"0.5", "0.5 Pixels"}, {"1", "1 Pixel"}, {"2", "2 Pixels"}, {"4", "4 Pixels"}},
   "-1"},
  {"video",
   "riverhorse_aspect_ratio",
   "Aspect Ratio",
   "Shape of the picture sent to the frontend.",
   "Display",
   "AspectRatio",
   {{"Auto (Game Native)", "Game Native"}, {"4:3", "4:3"}, {"16:9", "16:9"}, {"PAR 1:1", "Square Pixels"}},
   "Auto (Game Native)"},
  {"video",
   "riverhorse_crop_mode",
   "Crop",
   "Removes the overscan area or all borders. Frontend Setting follows the frontend's Crop Overscan setting.",
   nullptr,
   nullptr,
   {{"Frontend", "Frontend Setting"}, {"Overscan", "Overscan Area"}, {"None", "None"}, {"Borders", "All Borders"}},
   "Frontend",
   &ApplyCropMode},
  {"video",
   "riverhorse_deinterlacing",
   "Deinterlacing",
   "How interlaced video is shown.",
   "Display",
   "DeinterlacingMode",
   {{"Adaptive", "Adaptive"}, {"Weave", "Weave"}, {"Blend", "Blend"}, {"Disabled", "Disabled"}},
   "Adaptive"},
  {"video", "riverhorse_disable_interlacing", "Disable Interlacing",
   "Renders interlaced games progressively, without flicker. A few games need interlacing to show correctly.", "GPU",
   "DisableInterlacing", s_on_off, "true"},
  {"video", "riverhorse_force_ntsc_timings", "Force NTSC Timings",
   "Runs PAL games at 60 Hz. Most games then run faster than intended.", "GPU", "ForceNTSCTimings", s_on_off, "false"},
  {"video", "riverhorse_chroma_smoothing", "Chroma Smoothing (24-Bit)",
   "Smooths the colour blockiness of full-motion videos.", "GPU", "ChromaSmoothing24Bit", s_on_off, "false"},
  {"console",
   "riverhorse_region",
   "Console Region",
   "Region of the emulated console.",
   "Console",
   "Region",
   {{"Auto", "Auto-Detect"}, {"NTSC-U", "NTSC-U/C"}, {"NTSC-J", "NTSC-J"}, {"PAL", "PAL"}},
   "Auto"},
  {"console", "riverhorse_fast_boot", "Fast Boot", "Skips the BIOS logo.", "BIOS", "PatchFastBoot", s_on_off, "false"},
  {"console", "riverhorse_8mb_ram", "Enable 8 MB RAM (restart)",
   "Gives the console the 8 MB of the development units, which some hacks and translations need. Takes effect when "
   "content is loaded.",
   "Console", "Enable8MBRAM", s_on_off, "false"},
  {"patches", "riverhorse_widescreen_patch", "Widescreen Patch",
   "Applies the game's 16:9 widescreen patch from Riverhorse's patch database, if it has one, and shows the game at "
   "16:9 while Aspect Ratio is Game Native.",
   "Libretro", "WidescreenPatch", s_on_off, "false"},
  {"patches", "riverhorse_60fps_patch", "60 FPS Patch",
   "Applies the game's 60 FPS patch from Riverhorse's patch database, if it has one.", "Libretro", "FrameRatePatch",
   s_on_off, "false"},
  {"patches", "riverhorse_ppf_patches", "PPF Patches",
   "Applies a .ppf patch with the same name as the disc image, such as a fan translation. Takes effect when content "
   "is loaded.",
   "CDROM", "LoadImagePatches", s_on_off, "true"},
  {"console",
   "riverhorse_cpu_mode",
   "CPU Execution Mode",
   "The recompiler is fastest; the interpreters help with problem games.",
   "CPU",
   "ExecutionMode",
   {{"Recompiler", "Recompiler"}, {"CachedInterpreter", "Cached Interpreter"}, {"Interpreter", "Interpreter"}},
   "Recompiler"},
  {"console",
   "riverhorse_overclock",
   "CPU Speed",
   "Runs the CPU faster than the console. Can speed up slow games or break them.",
   nullptr,
   nullptr,
   {{"100", "100%"}, {"150", "150%"}, {"200", "200%"}, {"300", "300%"}, {"400", "400%"}},
   "100",
   &ApplyOverclock},
  {"cdrom",
   "riverhorse_cdrom_read_speedup",
   "CD-ROM Read Speed",
   "Reads data faster than the console to shorten loads.",
   "CDROM",
   "ReadSpeedup",
   {{"1", "1x (Console)"}, {"2", "2x"}, {"4", "4x"}, {"8", "8x"}, {"0", "Maximum"}},
   "1"},
  {"cdrom",
   "riverhorse_cdrom_seek_speedup",
   "CD-ROM Seek Speed",
   "Seeks faster than the console to shorten loads.",
   "CDROM",
   "SeekSpeedup",
   {{"1", "1x (Console)"}, {"2", "2x"}, {"4", "4x"}, {"8", "8x"}, {"0", "Maximum"}},
   "1"},
  {"cdrom", "riverhorse_load_to_ram", "Preload Disc to RAM", "Reads the whole disc into memory when it is loaded.",
   "CDROM", "LoadImageToRAM", s_on_off, "false"},
  {"input", "riverhorse_force_analog", "Start DualShocks in Analog Mode", "Turns analog mode on at reset.", nullptr,
   nullptr, s_on_off, "true", &ApplyForceAnalog},
  {"input",
   "riverhorse_analog_button",
   "DualShock Analog Button",
   "Button combination that presses the DualShock's Analog button, which switches analog mode on and off.",
   "Libretro",
   "AnalogButton",
   {{"Disabled", "Disabled"}, {"L3R3", "L3 + R3"}, {"SelectL1R1", "Select + L1 + R1"}},
   "Disabled"},
  {"input", "riverhorse_crosshair", "Light Gun Crosshair",
   "Draws a crosshair where each GunCon or Justifier is aiming.", "Libretro", "Crosshair", s_on_off, "true"},
  {"memory_cards",
   "riverhorse_memory_card_mode",
   "Memory Cards",
   "Per Game gives each game its own cards, named after the game. Shared uses the same cards for every game.",
   "MemoryCards",
   "Card1Type",
   {{"PerGameTitle", "Per Game"}, {"Shared", "Shared"}},
   "PerGameTitle"},
  {"memory_cards",
   "riverhorse_memory_card_2",
   "Memory Card 2",
   "Adds a second memory card.",
   nullptr,
   nullptr,
   {{"None", "Disabled"}, {"PerGameTitle", "Enabled"}},
   "None",
   &ApplyMemoryCard2},
  {"input",
   "riverhorse_multitap",
   "Multitap",
   "Connects a Multitap for up to four players per port. Users 3 to 5 are port 1's extra controllers, users 6 to 8 "
   "port 2's.",
   "ControllerPorts",
   "MultitapMode",
   {{"Disabled", "Disabled"}, {"Port1Only", "Port 1"}, {"Port2Only", "Port 2"}, {"BothPorts", "Both Ports"}},
   "Disabled"},
};

template<typename Definition>
static void FillDefinition(Definition* definition, const Option& option)
{
  definition->key = option.key;
  definition->desc = option.description;
  definition->info = option.info;
  definition->default_value = option.default_value;
  for (size_t i = 0; i < option.values.size() && i < RETRO_NUM_CORE_OPTION_VALUES_MAX - 1; i++)
    definition->values[i] = {option.values[i].value, option.values[i].label};
}

template<typename Definition>
static void FillDefinition(Definition* definition, const CheatOption& option)
{
  definition->key = option.key.c_str();
  definition->desc = option.description.c_str();
  definition->info = option.info.empty() ? nullptr : option.info.c_str();
  definition->default_value = "false";
  for (size_t i = 0; i < s_on_off.size(); i++)
    definition->values[i] = {s_on_off[i].value, s_on_off[i].label};
}

void Register(retro_environment_t environment)
{
  unsigned version = 0;
  if (environment(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version) && version >= 2)
  {
    static constexpr retro_core_option_v2_category categories[] = {
      {"video", "Video", "Renderer, resolution, filtering and picture shape."},
      {"pgxp", "PGXP", "Geometry precision fixes for wobbling polygons and warping textures."},
      {"patches", "Patches", "Widescreen and 60 FPS patches from Riverhorse's patch database."},
      {"console", "Console", "Region, BIOS and CPU."},
      {"cdrom", "CD-ROM", "Disc reading speed and preloading."},
      {"input", "Input", "Controllers and the Multitap."},
      {"memory_cards", "Memory Cards", "Per-game or shared memory cards."},
      {"cheats", "Cheats", "Codes from Riverhorse's cheat database for the loaded game."},
      {nullptr, nullptr, nullptr},
    };

    static std::vector<retro_core_option_v2_definition> definitions;
    definitions.clear();
    definitions.reserve(s_options.size() + s_cheat_options.size() + 1);
    for (const Option& option : s_options)
    {
      retro_core_option_v2_definition definition = {};
      FillDefinition(&definition, option);
      definition.category_key = option.category;
      definitions.push_back(definition);
    }
    for (const CheatOption& option : s_cheat_options)
    {
      retro_core_option_v2_definition definition = {};
      FillDefinition(&definition, option);
      definition.category_key = "cheats";
      definitions.push_back(definition);
    }
    definitions.push_back({});

    static retro_core_options_v2 options;
    options.categories = const_cast<retro_core_option_v2_category*>(categories);
    options.definitions = definitions.data();
    environment(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, &options);
    return;
  }

  static std::vector<retro_core_option_definition> definitions;
  definitions.clear();
  definitions.reserve(s_options.size() + s_cheat_options.size() + 1);
  for (const Option& option : s_options)
  {
    retro_core_option_definition definition = {};
    FillDefinition(&definition, option);
    definitions.push_back(definition);
  }
  for (const CheatOption& option : s_cheat_options)
  {
    retro_core_option_definition definition = {};
    FillDefinition(&definition, option);
    definitions.push_back(definition);
  }
  definitions.push_back({});

  environment(RETRO_ENVIRONMENT_SET_CORE_OPTIONS, definitions.data());
}

void SetCheatOptions(retro_environment_t environment, std::vector<CheatOption> options)
{
  s_cheat_options = std::move(options);
  Register(environment);
}

void ApplyCropMode(retro_environment_t environment, SettingsInterface& si, const char* value)
{
  if (std::strcmp(value, "Frontend") == 0)
  {
    bool show_overscan = false;
    value = (environment(RETRO_ENVIRONMENT_GET_OVERSCAN, &show_overscan) && show_overscan) ? "None" : "Overscan";
  }

  si.SetStringValue("Display", "CropMode", value);
}

void ApplyOverclock(retro_environment_t environment, SettingsInterface& si, const char* percent)
{
  si.SetBoolValue("CPU", "OverclockEnable", std::strcmp(percent, "100") != 0);
  si.SetStringValue("CPU", "OverclockNumerator", percent);
  si.SetIntValue("CPU", "OverclockDenominator", 100);
}

void ApplyMemoryCard2(retro_environment_t environment, SettingsInterface& si, const char* value)
{
  si.SetStringValue("MemoryCards", "Card2Type",
                    (std::strcmp(value, "None") == 0) ? value : si.GetStringValue("MemoryCards", "Card1Type").c_str());
}

void ApplyForceAnalog(retro_environment_t environment, SettingsInterface& si, const char* value)
{
  si.SetStringValue("Pad1", "ForceAnalogOnReset", value);
  si.SetStringValue("Pad2", "ForceAnalogOnReset", value);
}

void Apply(retro_environment_t environment, SettingsInterface& si)
{
  for (const Option& option : s_options)
  {
    retro_variable variable = {option.key, nullptr};
    const char* value = (environment(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value) ?
                          variable.value :
                          option.default_value;

    if (option.apply)
      option.apply(environment, si, value);
    else
      si.SetStringValue(option.section, option.setting, value);
  }
}

bool HaveChanged(retro_environment_t environment)
{
  bool updated = false;
  return environment(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated;
}

} // namespace LibretroCoreOptions
