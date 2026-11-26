// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#define IMGUI_DEFINE_MATH_OPERATORS

#include "game_card.h"
#include "game_list.h"
#include "host.h"
#include "settings.h"

#include "util/imgui_fullscreen.h"

#include "IconsFontAwesome5.h"
#include "imgui.h"
#include "imgui_internal.h"

#include <array>
#include <string_view>

namespace GameCard {
static constexpr float SHADE_FACTOR = 0.38f;
static constexpr float WATERMARK_ALPHA = 0.12f;

static constexpr std::array<u32, 10> PALETTE = {{
  0x2A8C82, // teal
  0x3468A8, // blue
  0x5A4FC4, // indigo
  0x8A3FA8, // purple
  0xB0397A, // magenta
  0xB8463A, // red
  0xC9772A, // orange
  0xA88A1E, // ochre
  0x3E8E4A, // green
  0x4D6478, // slate
}};

static u32 GetPaletteIndex(const GameList::Entry* entry);
} // namespace GameCard

using namespace ImGuiFullscreen;

u32 GameCard::GetPaletteIndex(const GameList::Entry* entry)
{
  const std::string_view key = entry->serial.empty() ? std::string_view(entry->title) : std::string_view(entry->serial);
  u32 hash = 2166136261u;
  for (const char ch : key)
    hash = (hash ^ static_cast<u8>(ch)) * 16777619u;

  return hash % static_cast<u32>(PALETTE.size());
}

std::string_view GameCard::GetDisplayTitle(std::string_view title)
{
  while (!title.empty() && title.back() == ')')
  {
    const size_t open = title.rfind(" (");
    if (open == std::string_view::npos || title.substr(open + 2).starts_with("Disc"))
      break;
    title = title.substr(0, open);
  }

  return title;
}

ImVec4 GameCard::GetAccentColor(const GameList::Entry* entry)
{
  return HEX_TO_IMVEC4(PALETTE[GetPaletteIndex(entry)], 255);
}

void GameCard::Draw(ImDrawList* dl, const ImRect& rect, const GameList::Entry* entry, float rounding)
{
  const ImVec4 accent = GetAccentColor(entry);
  const ImVec4 shade(accent.x * SHADE_FACTOR, accent.y * SHADE_FACTOR, accent.z * SHADE_FACTOR, 1.0f);
  const int vtx_start = dl->VtxBuffer.Size;
  dl->AddRectFilled(rect.Min, rect.Max, IM_COL32_WHITE, rounding);
  ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, vtx_start, dl->VtxBuffer.Size, rect.Min,
                                                ImVec2(rect.Min.x, rect.Max.y), ImGui::GetColorU32(accent),
                                                ImGui::GetColorU32(shade));

  const float padding = rect.GetWidth() * 0.08f;
  dl->PushClipRect(rect.Min, rect.Max, true);

  const float watermark_size = rect.GetHeight() * 0.75f;
  dl->AddText(g_large_font, watermark_size,
              ImVec2(rect.Max.x - watermark_size * 0.8f, rect.Max.y - watermark_size * 0.85f),
              IM_COL32(255, 255, 255, static_cast<u8>(WATERMARK_ALPHA * 255.0f)), ICON_FA_COMPACT_DISC);

  const float title_size = std::min(g_large_font->FontSize, rect.GetHeight() * 0.13f);
  const float wrap_width = rect.GetWidth() - padding * 2.0f;
  const ImRect title_clip(rect.Min + ImVec2(padding, padding), rect.Max - ImVec2(padding, padding * 2.0f));
  const ImVec4 title_clip_vec(title_clip.Min.x, title_clip.Min.y, title_clip.Max.x, title_clip.Max.y);
  const std::string_view title = GetDisplayTitle(entry->title);
  dl->AddText(g_large_font, title_size, title_clip.Min, IM_COL32(255, 255, 255, 255), title.data(),
              title.data() + title.length(), wrap_width, &title_clip_vec);

  const char* region = Settings::GetDiscRegionName(entry->region);
  const float badge_font_size = title_size * 0.62f;
  const ImVec2 badge_text_size = g_medium_font->CalcTextSizeA(badge_font_size, FLT_MAX, 0.0f, region);
  const ImVec2 badge_padding(badge_font_size * 0.5f, badge_font_size * 0.2f);
  const ImVec2 badge_min(rect.Min.x + padding, rect.Max.y - padding - badge_text_size.y - badge_padding.y * 2.0f);
  const ImVec2 badge_max = badge_min + badge_text_size + badge_padding * 2.0f;
  dl->AddRectFilled(badge_min, badge_max, IM_COL32(0, 0, 0, 110), (badge_max.y - badge_min.y) * 0.5f);
  dl->AddText(g_medium_font, badge_font_size, badge_min + badge_padding, IM_COL32(255, 255, 255, 230), region);

  dl->PopClipRect();
}
