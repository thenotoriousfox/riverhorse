// SPDX-FileCopyrightText: 2019-2023 Connor McLaughlin <stenzek@gmail.com> and contributors.
// SPDX-License-Identifier: GPL-3.0-only

#include "digital_controller.h"
#include "host.h"
#include "system.h"

#include "util/state_wrapper.h"

#include "IconsPromptFont.h"

#include "common/assert.h"
#include "common/bitutils.h"

DigitalController::DigitalController(u32 index) : Controller(index)
{
}

DigitalController::~DigitalController() = default;

ControllerType DigitalController::GetType() const
{
  return ControllerType::DigitalController;
}

void DigitalController::Reset()
{
  m_transfer_state = TransferState::Idle;
}

bool DigitalController::DoState(StateWrapper& sw, bool apply_input_state)
{
  if (!Controller::DoState(sw, apply_input_state))
    return false;

  u16 button_state = m_button_state;
  sw.Do(&button_state);
  if (apply_input_state)
    m_button_state = button_state;

  sw.Do(&m_transfer_state);
  return true;
}

float DigitalController::GetBindState(u32 index) const
{
  if (index < static_cast<u32>(Button::Count))
  {
    return static_cast<float>(((m_button_state >> index) & 1u) ^ 1u);
  }
  else
  {
    return 0.0f;
  }
}

void DigitalController::SetBindState(u32 index, float value)
{
  if (index >= static_cast<u32>(Button::Count))
    return;

  const bool pressed = (value >= 0.5f);
  const u16 bit = u16(1) << static_cast<u8>(index);
  if (pressed)
  {
    if (m_button_state & bit)
      System::SetRunaheadReplayFlag();

    m_button_state &= ~bit;
  }
  else
  {
    if (!(m_button_state & bit))
      System::SetRunaheadReplayFlag();

    m_button_state |= bit;
  }
}

u32 DigitalController::GetButtonStateBits() const
{
  return m_button_state ^ 0xFFFF;
}

void DigitalController::ResetTransferState()
{
  m_transfer_state = TransferState::Idle;
}

bool DigitalController::Transfer(const u8 data_in, u8* data_out)
{
  static constexpr u16 ID = 0x5A41;

  switch (m_transfer_state)
  {
    case TransferState::Idle:
    {
      *data_out = 0xFF;

      if (data_in == 0x01)
      {
        m_transfer_state = TransferState::Ready;
        return true;
      }
      return false;
    }

    case TransferState::Ready:
    {
      if (data_in == 0x42)
      {
        *data_out = Truncate8(ID);
        m_transfer_state = TransferState::IDMSB;
        return true;
      }

      *data_out = 0xFF;
      return false;
    }

    case TransferState::IDMSB:
    {
      *data_out = Truncate8(ID >> 8);
      m_transfer_state = TransferState::ButtonsLSB;
      return true;
    }

    case TransferState::ButtonsLSB:
    {
      *data_out = Truncate8(m_button_state & m_grounded_buttons_mask);
      m_transfer_state = TransferState::ButtonsMSB;
      return true;
    }

    case TransferState::ButtonsMSB:
      *data_out = Truncate8((m_button_state & m_grounded_buttons_mask) >> 8);
      m_transfer_state = TransferState::Idle;
      return false;

    default:
      UnreachableCode();
  }
}

std::unique_ptr<DigitalController> DigitalController::Create(u32 index)
{
  return std::make_unique<DigitalController>(index);
}

static const Controller::ControllerBindingInfo s_binding_info[] = {
#define BUTTON(name, display_name, icon_name, button, genb)                                                            \
  {                                                                                                                    \
    name, display_name, icon_name, static_cast<u32>(button), InputBindingInfo::Type::Button, genb                      \
  }

  // clang-format off
  BUTTON("Up", TRANSLATE_NOOP("DigitalController", "D-Pad Up"), ICON_PF_DPAD_UP, DigitalController::Button::Up, GenericInputBinding::DPadUp),
  BUTTON("Right", TRANSLATE_NOOP("DigitalController", "D-Pad Right"), ICON_PF_DPAD_RIGHT, DigitalController::Button::Right, GenericInputBinding::DPadRight),
  BUTTON("Down", TRANSLATE_NOOP("DigitalController", "D-Pad Down"), ICON_PF_DPAD_DOWN, DigitalController::Button::Down, GenericInputBinding::DPadDown),
  BUTTON("Left", TRANSLATE_NOOP("DigitalController", "D-Pad Left"), ICON_PF_DPAD_LEFT, DigitalController::Button::Left, GenericInputBinding::DPadLeft),
  BUTTON("Triangle", TRANSLATE_NOOP("DigitalController", "Triangle"), ICON_PF_BUTTON_TRIANGLE, DigitalController::Button::Triangle, GenericInputBinding::Triangle),
  BUTTON("Circle", TRANSLATE_NOOP("DigitalController", "Circle"), ICON_PF_BUTTON_CIRCLE, DigitalController::Button::Circle, GenericInputBinding::Circle),
  BUTTON("Cross", TRANSLATE_NOOP("DigitalController", "Cross"), ICON_PF_BUTTON_CROSS, DigitalController::Button::Cross, GenericInputBinding::Cross),
  BUTTON("Square", TRANSLATE_NOOP("DigitalController", "Square"), ICON_PF_BUTTON_SQUARE, DigitalController::Button::Square, GenericInputBinding::Square),
  BUTTON("Select", TRANSLATE_NOOP("DigitalController", "Select"), ICON_PF_SELECT_SHARE, DigitalController::Button::Select, GenericInputBinding::Select),
  BUTTON("Start", TRANSLATE_NOOP("DigitalController", "Start"), ICON_PF_START, DigitalController::Button::Start, GenericInputBinding::Start),
  BUTTON("L1", TRANSLATE_NOOP("DigitalController", "L1"), ICON_PF_LEFT_SHOULDER_L1, DigitalController::Button::L1, GenericInputBinding::L1),
  BUTTON("R1", TRANSLATE_NOOP("DigitalController", "R1"), ICON_PF_RIGHT_SHOULDER_R1, DigitalController::Button::R1, GenericInputBinding::R1),
  BUTTON("L2", TRANSLATE_NOOP("DigitalController", "L2"), ICON_PF_LEFT_TRIGGER_L2, DigitalController::Button::L2, GenericInputBinding::L2),
  BUTTON("R2", TRANSLATE_NOOP("DigitalController", "R2"), ICON_PF_RIGHT_TRIGGER_R2, DigitalController::Button::R2, GenericInputBinding::R2),
// clang-format on

#undef BUTTON
};

static const char* s_variant_settings[] = {TRANSLATE_NOOP("DigitalController", "Standard Controller"),
                                           TRANSLATE_NOOP("DigitalController", "Pop'n Controller"),
                                           TRANSLATE_NOOP("DigitalController", "Densha de Go! Controller"), nullptr};

static const SettingInfo s_settings[] = {
  {SettingInfo::Type::IntegerList, "ControllerVariant", TRANSLATE_NOOP("DigitalController", "Controller Variant"),
   TRANSLATE_NOOP("DigitalController",
                  "Reports the buttons that a Pop'n or Densha de Go! controller holds down, so games recognise it."),
   "0", "0", "2", nullptr, nullptr, s_variant_settings, 0.0f}};

const Controller::ControllerInfo DigitalController::INFO = {ControllerType::DigitalController,
                                                            "DigitalController",
                                                            TRANSLATE_NOOP("ControllerType", "Digital Controller"),
                                                            ICON_PF_GAMEPAD,
                                                            s_binding_info,
                                                            s_settings,
                                                            Controller::VibrationCapabilities::NoVibration};

void DigitalController::LoadSettings(SettingsInterface& si, const char* section)
{
  Controller::LoadSettings(si, section);

  static constexpr auto bit = [](Button button) { return static_cast<u16>(1u << static_cast<u8>(button)); };
  static constexpr u16 POPN_GROUNDED = bit(Button::Left) | bit(Button::Right) | bit(Button::Down);
  static constexpr u16 DENSHA_GROUNDED = bit(Button::Up) | bit(Button::Down);

  const s32 default_variant = si.GetBoolValue(section, "ForcePopnControllerMode", false) ? 1 : 0;
  switch (si.GetIntValue(section, "ControllerVariant", default_variant))
  {
    case 1:
      m_grounded_buttons_mask = static_cast<u16>(~POPN_GROUNDED);
      break;
    case 2:
      m_grounded_buttons_mask = static_cast<u16>(~DENSHA_GROUNDED);
      break;
    default:
      m_grounded_buttons_mask = UINT16_C(0xFFFF);
      break;
  }
}
