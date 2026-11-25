// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#define IMGUI_DEFINE_MATH_OPERATORS

#include "rewind_timeline.h"
#include "fullscreen_ui.h"
#include "host.h"
#include "system.h"

#include "util/gpu_device.h"
#include "util/imgui_fullscreen.h"

#include "common/small_string.h"

#include "IconsFontAwesome5.h"
#include "IconsPromptFont.h"
#include "imgui.h"
#include "imgui_internal.h"

#include <array>
#include <cmath>
#include <optional>

namespace RewindTimeline {
static constexpr float THUMBNAIL_HEIGHT = 120.0f;
static constexpr float THUMBNAIL_SPACING = 12.0f;
static constexpr float SELECTED_BORDER_THICKNESS = 4.0f;

static void SetFooterText();
static void FormatAge(SmallStringBase& dest, float seconds);
static void ShowSnapshot(u32 index);
static void Finish(std::optional<u32> continue_from);

static bool s_open = false;
static bool s_scroll_to_selected = false;
static u32 s_selected = 0;
} // namespace RewindTimeline

using namespace ImGuiFullscreen;

bool RewindTimeline::Open()
{
  if (!System::BeginRewindTimeline())
    return false;

  s_open = true;
  ShowSnapshot(System::GetRewindSnapshotCount() - 1);
  QueueResetFocus(FocusResetType::PopupOpened);
  return true;
}

bool RewindTimeline::IsOpen()
{
  return s_open;
}

void RewindTimeline::Close()
{
  if (s_open)
    Finish(s_selected);
}

void RewindTimeline::ShowSnapshot(u32 index)
{
  s_selected = index;
  s_scroll_to_selected = true;
  Host::RunOnCPUThread([index]() { System::ShowRewindSnapshot(index); });
}

void RewindTimeline::Finish(std::optional<u32> continue_from)
{
  s_open = false;
  Host::RunOnCPUThread([continue_from]() { System::EndRewindTimeline(continue_from); });
  if (continue_from.has_value())
    FullscreenUI::ReturnToMainWindow();
  else
    FullscreenUI::ReturnToPreviousWindow();
}

void RewindTimeline::FormatAge(SmallStringBase& dest, float seconds)
{
  if (seconds < 60.0f)
  {
    dest.format(TRANSLATE_FS("RewindTimeline", "{:.1f}s ago"), seconds);
  }
  else
  {
    const u32 whole_seconds = static_cast<u32>(seconds);
    dest.format(TRANSLATE_FS("RewindTimeline", "{}:{:02} ago"), whole_seconds / 60, whole_seconds % 60);
  }
}

void RewindTimeline::SetFooterText()
{
  if (IsGamepadInputSource())
  {
    SetFullscreenFooterText(
      std::array{std::make_pair(ICON_PF_XBOX_DPAD_LEFT_RIGHT, TRANSLATE_SV("RewindTimeline", "Scrub")),
                 std::make_pair(ICON_PF_BUTTON_A, TRANSLATE_SV("RewindTimeline", "Continue From Here")),
                 std::make_pair(ICON_PF_BUTTON_B, TRANSLATE_SV("RewindTimeline", "Cancel"))});
  }
  else
  {
    SetFullscreenFooterText(
      std::array{std::make_pair(ICON_PF_ARROW_LEFT ICON_PF_ARROW_RIGHT, TRANSLATE_SV("RewindTimeline", "Scrub")),
                 std::make_pair(ICON_PF_ENTER, TRANSLATE_SV("RewindTimeline", "Continue From Here")),
                 std::make_pair(ICON_PF_ESC, TRANSLATE_SV("RewindTimeline", "Cancel"))});
  }
}

void RewindTimeline::Draw()
{
  const u32 count = System::GetRewindSnapshotCount();
  if (!System::IsRewindTimelineActive() || count == 0)
  {
    s_open = false;
    FullscreenUI::ReturnToPreviousWindow();
    return;
  }
  s_selected = std::min(s_selected, count - 1);

  const ImGuiIO& io = ImGui::GetIO();
  const ImGuiStyle& style = ImGui::GetStyle();
  const float footer_top = io.DisplaySize.y - LayoutScale(LAYOUT_FOOTER_HEIGHT);
  const float heading_height = LayoutScale(LAYOUT_MENU_BUTTON_HEIGHT_NO_SUMMARY) +
                               (LayoutScale(LAYOUT_MENU_BUTTON_Y_PADDING) * 2.0f) + LayoutScale(2.0f);
  const float label_spacing = LayoutScale(4.0f);
  const float thumbnail_height = LayoutScale(THUMBNAIL_HEIGHT);
  const float item_height = (style.FramePadding.y * 2.0f) + thumbnail_height + label_spacing + g_medium_font->FontSize;
  const float strip_height = item_height + LayoutScale(THUMBNAIL_SPACING) * 2.0f + style.ScrollbarSize;

  SmallString age;
  bool cancel = false;
  std::optional<u32> continue_from;

  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, footer_top));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);

  const char* window_title = TRANSLATE("RewindTimeline", "Rewind Timeline");
  ImGui::OpenPopup(window_title);
  if (ImGui::BeginPopupModal(window_title, nullptr,
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground))
  {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ModAlpha(UIPrimaryColor, 0.9f));
    if (ImGui::BeginChild("rewind_titlebar", ImVec2(io.DisplaySize.x, heading_height), false,
                          ImGuiWindowFlags_NavFlattened))
    {
      BeginNavBar();
      if (NavButton(ICON_FA_BACKWARD, true, true))
        cancel = true;

      FormatAge(age, System::GetRewindSnapshotAge(s_selected));
      NavTitle(
        SmallString::from_format("{} {} ({})", ICON_FA_HISTORY, TRANSLATE_SV("RewindTimeline", "Rewind Timeline"), age)
          .c_str());
      EndNavBar();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::SetCursorPos(ImVec2(0.0f, footer_top - strip_height));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ModAlpha(UIBackgroundColor, 0.9f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, LayoutScale(THUMBNAIL_SPACING, THUMBNAIL_SPACING));
    if (ImGui::BeginChild("rewind_strip", ImVec2(io.DisplaySize.x, strip_height), false,
                          ImGuiWindowFlags_NavFlattened | ImGuiWindowFlags_HorizontalScrollbar |
                            ImGuiWindowFlags_AlwaysUseWindowPadding))
    {
      const bool reset_focus = IsFocusResetQueued();
      ResetFocusHere();
      ImGuiWindow* window = ImGui::GetCurrentWindow();
      ImDrawList* dl = ImGui::GetWindowDrawList();

      for (u32 i = 0; i < count; i++)
      {
        if (i > 0)
          ImGui::SameLine(0.0f, LayoutScale(THUMBNAIL_SPACING));

        GPUTexture* const thumbnail = System::GetRewindSnapshotThumbnail(i);
        const float aspect =
          thumbnail ? (static_cast<float>(thumbnail->GetWidth()) / static_cast<float>(thumbnail->GetHeight())) :
                      (4.0f / 3.0f);
        const ImVec2 image_size(std::floor(thumbnail_height * aspect), thumbnail_height);
        const ImVec2 item_size(image_size.x + style.FramePadding.x * 2.0f, item_height);

        const ImGuiID id = window->GetID(static_cast<int>(i));
        const ImVec2 pos = window->DC.CursorPos;
        const ImRect bb(pos, pos + item_size);
        ImGui::ItemSize(item_size);
        if (i == s_selected && s_scroll_to_selected)
        {
          ImGui::ScrollToRect(window, bb, ImGuiScrollFlags_KeepVisibleCenterX);
          s_scroll_to_selected = false;
        }
        if (reset_focus && i == s_selected)
          ImGui::SetFocusID(id, window);
        if (!ImGui::ItemAdd(bb, id))
          continue;

        bool hovered, held;
        const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, 0);
        const bool focused = (GImGui->NavId == id && !GImGui->NavDisableHighlight);
        if (focused && i != s_selected)
          ShowSnapshot(i);

        if (i == s_selected || hovered)
        {
          const ImU32 col = ImGui::GetColorU32((i == s_selected) ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered);
          DrawMenuButtonFrame(bb.Min, bb.Max, col, true, 0.0f);
        }
        if (i == s_selected)
        {
          dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(UISecondaryColor), 0.0f, 0,
                      LayoutScale(SELECTED_BORDER_THICKNESS));
        }

        const ImVec2 image_min = bb.Min + style.FramePadding;
        if (thumbnail)
          dl->AddImage(thumbnail, image_min, image_min + image_size);
        else
          dl->AddRectFilled(image_min, image_min + image_size, ImGui::GetColorU32(UIBackgroundColor));

        FormatAge(age, System::GetRewindSnapshotAge(i));
        const ImVec2 label_pos(image_min.x, image_min.y + image_size.y + label_spacing);
        const ImRect label_bb(label_pos, ImVec2(image_min.x + image_size.x, label_pos.y + g_medium_font->FontSize));
        ImGui::PushFont(g_medium_font);
        ImGui::RenderTextClipped(label_bb.Min, label_bb.Max, age.c_str(), age.end_ptr(), nullptr, ImVec2(0.5f, 0.0f),
                                 &label_bb);
        ImGui::PopFont();

        if (pressed)
        {
          if (i == s_selected)
            continue_from = i;
          else
            ShowSnapshot(i);
        }
      }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::EndPopup();
  }
  ImGui::PopStyleVar(5);

  SetFooterText();

  if (continue_from.has_value())
    Finish(continue_from);
  else if (cancel || WantsToCloseMenu())
    Finish(std::nullopt);
}
