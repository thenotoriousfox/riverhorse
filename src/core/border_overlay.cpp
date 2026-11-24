// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "border_overlay.h"
#include "settings.h"

#include "util/image.h"
#include "util/imgui_fullscreen.h"

#include "common/log.h"
#include "common/path.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <optional>
#include <string>

Log_SetChannel(BorderOverlay);

namespace BorderOverlay {

static constexpr u32 OPAQUE_ALPHA = 128;

struct Layout
{
  u32 image_width;
  u32 image_height;
  GSVector4i screen;
};

static std::optional<Layout> LoadLayout(const std::string& path);
static GSVector4 GetImageRect(const Layout& layout, s32 window_width, s32 window_height);

static std::mutex s_mutex;
static std::string s_loaded_path;
static std::optional<Layout> s_layout;

} // namespace BorderOverlay

std::optional<BorderOverlay::Layout> BorderOverlay::LoadLayout(const std::string& path)
{
  RGBA8Image image;
  if (!image.LoadFromFile(path.c_str()))
  {
    ERROR_LOG("Failed to load border overlay '{}'.", Path::GetFileName(path));
    return std::nullopt;
  }

  const u32 width = image.GetWidth();
  const u32 height = image.GetHeight();
  const u32 center_x = width / 2;
  const u32 center_y = height / 2;
  const auto is_screen = [&image](u32 x, u32 y) { return (image.GetPixel(x, y) >> 24) < OPAQUE_ALPHA; };
  if (width == 0 || height == 0 || !is_screen(center_x, center_y))
  {
    ERROR_LOG("Border overlay '{}' has no transparent area in its middle for the game.", Path::GetFileName(path));
    return std::nullopt;
  }

  u32 left = center_x, right = center_x, top = center_y, bottom = center_y;
  while (left > 0 && is_screen(left - 1, center_y))
    left--;
  while (right + 1 < width && is_screen(right + 1, center_y))
    right++;
  while (top > 0 && is_screen(center_x, top - 1))
    top--;
  while (bottom + 1 < height && is_screen(center_x, bottom + 1))
    bottom++;

  INFO_LOG("Border overlay '{}': {}x{}, game area {},{} to {},{}.", Path::GetFileName(path), width, height, left, top,
           right + 1, bottom + 1);
  return Layout{width, height,
                GSVector4i(static_cast<s32>(left), static_cast<s32>(top), static_cast<s32>(right + 1),
                           static_cast<s32>(bottom + 1))};
}

GSVector4 BorderOverlay::GetImageRect(const Layout& layout, s32 window_width, s32 window_height)
{
  const float scale = std::min(static_cast<float>(window_width) / static_cast<float>(layout.image_width),
                               static_cast<float>(window_height) / static_cast<float>(layout.image_height));
  const float width = static_cast<float>(layout.image_width) * scale;
  const float height = static_cast<float>(layout.image_height) * scale;
  const float left = (static_cast<float>(window_width) - width) * 0.5f;
  const float top = (static_cast<float>(window_height) - height) * 0.5f;
  return GSVector4(left, top, left + width, top + height);
}

void BorderOverlay::Update()
{
  std::unique_lock lock(s_mutex);
  if (s_loaded_path == g_settings.display_border_overlay_path)
    return;

  s_loaded_path = g_settings.display_border_overlay_path;
  s_layout = s_loaded_path.empty() ? std::nullopt : LoadLayout(s_loaded_path);
}

GSVector4i BorderOverlay::GetScreenRect(s32 window_width, s32 window_height)
{
  std::unique_lock lock(s_mutex);
  if (!s_layout.has_value())
    return GSVector4i(0, 0, window_width, window_height);

  const GSVector4 image_rect = GetImageRect(s_layout.value(), window_width, window_height);
  const float scale = (image_rect.z - image_rect.x) / static_cast<float>(s_layout->image_width);
  const auto to_window = [scale](s32 image_pos, float origin) {
    return static_cast<s32>(std::floor(static_cast<float>(image_pos) * scale + origin));
  };
  return GSVector4i(to_window(s_layout->screen.left, image_rect.x), to_window(s_layout->screen.top, image_rect.y),
                    to_window(s_layout->screen.right, image_rect.x), to_window(s_layout->screen.bottom, image_rect.y));
}

void BorderOverlay::Draw()
{
  std::unique_lock lock(s_mutex);
  if (!s_layout.has_value())
    return;

  GPUTexture* const texture = ImGuiFullscreen::GetCachedTexture(s_loaded_path);
  if (!texture)
    return;

  const ImVec2 window_size = ImGui::GetIO().DisplaySize;
  const GSVector4 rect =
    GetImageRect(s_layout.value(), static_cast<s32>(window_size.x), static_cast<s32>(window_size.y));
  ImGui::GetBackgroundDrawList()->AddImage(reinterpret_cast<ImTextureID>(texture), ImVec2(rect.x, rect.y),
                                           ImVec2(rect.z, rect.w));
}
