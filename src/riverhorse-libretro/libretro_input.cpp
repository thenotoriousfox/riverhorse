// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "libretro_input.h"

#include "core/analog_controller.h"
#include "core/controller.h"
#include "core/gpu.h"
#include "core/guncon.h"
#include "core/justifier.h"
#include "core/negcon.h"
#include "core/pad.h"
#include "core/playstation_mouse.h"
#include "core/settings.h"

#include "util/input_manager.h"

#include "common/settings_interface.h"
#include "common/small_string.h"

#include <algorithm>
#include <array>
#include <span>
#include <tuple>

namespace LibretroInput {

static constexpr unsigned RETRO_DEVICE_DUALSHOCK = RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 1);
static constexpr unsigned RETRO_DEVICE_NEGCON = RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 2);
static constexpr unsigned RETRO_DEVICE_GUNCON = RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_LIGHTGUN, 0);
static constexpr unsigned RETRO_DEVICE_JUSTIFIER = RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_LIGHTGUN, 1);
static constexpr u32 PORT_COUNT = NUM_CONTROLLER_AND_CARD_PORTS;
static constexpr float ANALOG_RANGE = 32767.0f;

static constexpr std::array<std::pair<unsigned, AnalogController::Button>, 16> s_joypad_mapping = {{
  {RETRO_DEVICE_ID_JOYPAD_B, AnalogController::Button::Cross},
  {RETRO_DEVICE_ID_JOYPAD_A, AnalogController::Button::Circle},
  {RETRO_DEVICE_ID_JOYPAD_Y, AnalogController::Button::Square},
  {RETRO_DEVICE_ID_JOYPAD_X, AnalogController::Button::Triangle},
  {RETRO_DEVICE_ID_JOYPAD_SELECT, AnalogController::Button::Select},
  {RETRO_DEVICE_ID_JOYPAD_START, AnalogController::Button::Start},
  {RETRO_DEVICE_ID_JOYPAD_UP, AnalogController::Button::Up},
  {RETRO_DEVICE_ID_JOYPAD_DOWN, AnalogController::Button::Down},
  {RETRO_DEVICE_ID_JOYPAD_LEFT, AnalogController::Button::Left},
  {RETRO_DEVICE_ID_JOYPAD_RIGHT, AnalogController::Button::Right},
  {RETRO_DEVICE_ID_JOYPAD_L, AnalogController::Button::L1},
  {RETRO_DEVICE_ID_JOYPAD_R, AnalogController::Button::R1},
  {RETRO_DEVICE_ID_JOYPAD_L2, AnalogController::Button::L2},
  {RETRO_DEVICE_ID_JOYPAD_R2, AnalogController::Button::R2},
  {RETRO_DEVICE_ID_JOYPAD_L3, AnalogController::Button::L3},
  {RETRO_DEVICE_ID_JOYPAD_R3, AnalogController::Button::R3},
}};

static constexpr std::array<std::pair<unsigned, NeGcon::Button>, 8> s_negcon_button_mapping = {{
  {RETRO_DEVICE_ID_JOYPAD_START, NeGcon::Button::Start},
  {RETRO_DEVICE_ID_JOYPAD_UP, NeGcon::Button::Up},
  {RETRO_DEVICE_ID_JOYPAD_DOWN, NeGcon::Button::Down},
  {RETRO_DEVICE_ID_JOYPAD_LEFT, NeGcon::Button::Left},
  {RETRO_DEVICE_ID_JOYPAD_RIGHT, NeGcon::Button::Right},
  {RETRO_DEVICE_ID_JOYPAD_R, NeGcon::Button::R},
  {RETRO_DEVICE_ID_JOYPAD_A, NeGcon::Button::A},
  {RETRO_DEVICE_ID_JOYPAD_X, NeGcon::Button::B},
}};

static retro_input_poll_t s_input_poll;
static retro_input_state_t s_input_state;
static std::array<unsigned, PORT_COUNT> s_port_devices = {
  RETRO_DEVICE_DUALSHOCK, RETRO_DEVICE_DUALSHOCK, RETRO_DEVICE_DUALSHOCK, RETRO_DEVICE_DUALSHOCK,
  RETRO_DEVICE_DUALSHOCK, RETRO_DEVICE_DUALSHOCK, RETRO_DEVICE_DUALSHOCK, RETRO_DEVICE_DUALSHOCK};

static ControllerType GetControllerType(unsigned device);
static float GetAnalogButton(u32 port, unsigned id);
static void UpdatePad(u32 port, Controller* controller);
static void UpdateNeGcon(u32 port, Controller* controller);
static void UpdateLightGun(u32 port, Controller* controller, u32 trigger, u32 shoot_offscreen,
                           std::span<const std::pair<unsigned, u32>> buttons);
static void UpdateMouse(u32 port, Controller* controller);

} // namespace LibretroInput

void LibretroInput::SetPollCallback(retro_input_poll_t input_poll)
{
  s_input_poll = input_poll;
}

void LibretroInput::SetStateCallback(retro_input_state_t input_state)
{
  s_input_state = input_state;
}

ControllerType LibretroInput::GetControllerType(unsigned device)
{
  switch (device)
  {
    case RETRO_DEVICE_NONE:
      return ControllerType::None;
    case RETRO_DEVICE_JOYPAD:
      return ControllerType::DigitalController;
    case RETRO_DEVICE_NEGCON:
      return ControllerType::NeGcon;
    case RETRO_DEVICE_GUNCON:
      return ControllerType::GunCon;
    case RETRO_DEVICE_JUSTIFIER:
      return ControllerType::Justifier;
    case RETRO_DEVICE_MOUSE:
      return ControllerType::PlayStationMouse;
    default:
      return ControllerType::AnalogController;
  }
}

void LibretroInput::RegisterControllers(retro_environment_t environment)
{
  static constexpr retro_controller_description types[] = {
    {"DualShock", RETRO_DEVICE_DUALSHOCK},
    {"Digital Controller", RETRO_DEVICE_JOYPAD},
    {"NeGcon", RETRO_DEVICE_NEGCON},
    {"GunCon", RETRO_DEVICE_GUNCON},
    {"Justifier", RETRO_DEVICE_JUSTIFIER},
    {"PlayStation Mouse", RETRO_DEVICE_MOUSE},
    {"None", RETRO_DEVICE_NONE},
  };
  static constexpr retro_controller_info ports[] = {
    {types, std::size(types)}, {types, std::size(types)}, {types, std::size(types)}, {types, std::size(types)},
    {types, std::size(types)}, {types, std::size(types)}, {types, std::size(types)}, {types, std::size(types)},
    {nullptr, 0},
  };
  static_assert(std::size(ports) == PORT_COUNT + 1);
  environment(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, const_cast<retro_controller_info*>(ports));

  static constexpr retro_input_descriptor descriptors[] = {
#define PORT_BUTTONS(port)                                                                                             \
  {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "D-Pad Left"},                                           \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "D-Pad Up"},                                             \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "D-Pad Down"},                                         \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right"},                                       \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Cross"},                                                 \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Circle"},                                                \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "Triangle"},                                              \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Square"},                                                \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "L1"},                                                    \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "R1"},                                                    \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, "L2"},                                                   \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, "R2"},                                                   \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3, "L3"},                                                   \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3, "R3"},                                                   \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select"},                                           \
    {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start"},                                             \
    {port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Stick X"},             \
    {port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Stick Y"},             \
    {port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Stick X"},           \
    {port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Stick Y"}
    PORT_BUTTONS(0), PORT_BUTTONS(1), PORT_BUTTONS(2), PORT_BUTTONS(3),
    PORT_BUTTONS(4), PORT_BUTTONS(5), PORT_BUTTONS(6), PORT_BUTTONS(7),
#undef PORT_BUTTONS
    {0, 0, 0, 0, nullptr},
  };
  environment(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, const_cast<retro_input_descriptor*>(descriptors));
}

bool LibretroInput::SetPortDevice(u32 port, unsigned device)
{
  if (port >= PORT_COUNT)
    return false;

  s_port_devices[port] = device;
  return true;
}

void LibretroInput::ApplyPortDevices(SettingsInterface& si)
{
  Settings multitap_settings;
  multitap_settings.multitap_mode =
    Settings::ParseMultitapModeName(si.GetStringValue("ControllerPorts", "MultitapMode", "").c_str())
      .value_or(Settings::DEFAULT_MULTITAP_MODE);

  for (u32 port = 0; port < PORT_COUNT; port++)
  {
    const auto [console_port, slot] = Controller::ConvertPadToPortAndSlot(port);
    const ControllerType type = (slot == 0 || multitap_settings.IsMultitapPortEnabled(console_port)) ?
                                  GetControllerType(s_port_devices[port]) :
                                  ControllerType::None;
    si.SetStringValue(TinyString::from_format("Pad{}", port + 1), "Type", Controller::GetControllerInfo(type)->name);
  }
}

float LibretroInput::GetAnalogButton(u32 port, unsigned id)
{
  const float analog =
    static_cast<float>(s_input_state(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_BUTTON, id)) / ANALOG_RANGE;
  const float digital = (s_input_state(port, RETRO_DEVICE_JOYPAD, 0, id) != 0) ? 1.0f : 0.0f;
  return std::clamp(std::max(analog, digital), 0.0f, 1.0f);
}

void LibretroInput::UpdatePad(u32 port, Controller* controller)
{
  for (const auto& [retro_id, button] : s_joypad_mapping)
  {
    const bool pressed = (s_input_state(port, RETRO_DEVICE_JOYPAD, 0, retro_id) != 0);
    controller->SetBindState(static_cast<u32>(button), pressed ? 1.0f : 0.0f);
  }

  if (controller->GetType() != ControllerType::AnalogController)
    return;

  static constexpr std::array<std::tuple<unsigned, unsigned, AnalogController::HalfAxis, AnalogController::HalfAxis>, 4>
    axes = {{
      {RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, AnalogController::HalfAxis::LLeft,
       AnalogController::HalfAxis::LRight},
      {RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, AnalogController::HalfAxis::LUp,
       AnalogController::HalfAxis::LDown},
      {RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, AnalogController::HalfAxis::RLeft,
       AnalogController::HalfAxis::RRight},
      {RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, AnalogController::HalfAxis::RUp,
       AnalogController::HalfAxis::RDown},
    }};
  for (const auto& [index, id, negative, positive] : axes)
  {
    const float value = static_cast<float>(s_input_state(port, RETRO_DEVICE_ANALOG, index, id)) / ANALOG_RANGE;
    const u32 base = static_cast<u32>(AnalogController::Button::Count);
    controller->SetBindState(base + static_cast<u32>(negative), std::max(-value, 0.0f));
    controller->SetBindState(base + static_cast<u32>(positive), std::max(value, 0.0f));
  }
}

void LibretroInput::UpdateNeGcon(u32 port, Controller* controller)
{
  for (const auto& [retro_id, button] : s_negcon_button_mapping)
  {
    const bool pressed = (s_input_state(port, RETRO_DEVICE_JOYPAD, 0, retro_id) != 0);
    controller->SetBindState(static_cast<u32>(button), pressed ? 1.0f : 0.0f);
  }

  const u32 base = static_cast<u32>(NeGcon::Button::Count);
  const float steering =
    static_cast<float>(s_input_state(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,
                                     RETRO_DEVICE_ID_ANALOG_X)) /
    ANALOG_RANGE;
  controller->SetBindState(base + static_cast<u32>(NeGcon::HalfAxis::SteeringLeft), std::max(-steering, 0.0f));
  controller->SetBindState(base + static_cast<u32>(NeGcon::HalfAxis::SteeringRight), std::max(steering, 0.0f));
  controller->SetBindState(base + static_cast<u32>(NeGcon::HalfAxis::I),
                           std::max(GetAnalogButton(port, RETRO_DEVICE_ID_JOYPAD_R2),
                                    GetAnalogButton(port, RETRO_DEVICE_ID_JOYPAD_B)));
  controller->SetBindState(base + static_cast<u32>(NeGcon::HalfAxis::II),
                           std::max(GetAnalogButton(port, RETRO_DEVICE_ID_JOYPAD_L2),
                                    GetAnalogButton(port, RETRO_DEVICE_ID_JOYPAD_Y)));
  controller->SetBindState(base + static_cast<u32>(NeGcon::HalfAxis::L), GetAnalogButton(port, RETRO_DEVICE_ID_JOYPAD_L));
}

void LibretroInput::UpdateLightGun(u32 port, Controller* controller, u32 trigger, u32 shoot_offscreen,
                                   std::span<const std::pair<unsigned, u32>> buttons)
{
  const bool offscreen = (s_input_state(port, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN) != 0);
  float window_x = -1.0f, window_y = -1.0f;
  if (!offscreen && g_gpu)
  {
    const float picture_x =
      (static_cast<float>(s_input_state(port, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X)) +
       ANALOG_RANGE) /
      (2.0f * ANALOG_RANGE);
    const float picture_y =
      (static_cast<float>(s_input_state(port, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y)) +
       ANALOG_RANGE) /
      (2.0f * ANALOG_RANGE);
    g_gpu->ConvertPictureCoordinatesToScreenCoordinates(picture_x, picture_y, &window_x, &window_y);
  }
  InputManager::UpdatePointerAbsolutePosition(0, window_x, window_y);

  const bool trigger_pressed = (s_input_state(port, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_TRIGGER) != 0);
  const bool reload_pressed = (s_input_state(port, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_RELOAD) != 0);
  controller->SetBindState(trigger, (trigger_pressed && !offscreen) ? 1.0f : 0.0f);
  controller->SetBindState(shoot_offscreen, (reload_pressed || (trigger_pressed && offscreen)) ? 1.0f : 0.0f);
  for (const auto& [retro_id, binding] : buttons)
    controller->SetBindState(binding, (s_input_state(port, RETRO_DEVICE_LIGHTGUN, 0, retro_id) != 0) ? 1.0f : 0.0f);
}

void LibretroInput::UpdateMouse(u32 port, Controller* controller)
{
  controller->SetBindState(static_cast<u32>(PlayStationMouse::Binding::Left),
                           (s_input_state(port, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT) != 0) ? 1.0f : 0.0f);
  controller->SetBindState(static_cast<u32>(PlayStationMouse::Binding::Right),
                           (s_input_state(port, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT) != 0) ? 1.0f :
                                                                                                           0.0f);
  controller->SetBindState(static_cast<u32>(PlayStationMouse::Binding::PointerX),
                           static_cast<float>(s_input_state(port, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X)));
  controller->SetBindState(static_cast<u32>(PlayStationMouse::Binding::PointerY),
                           static_cast<float>(s_input_state(port, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y)));
}

void LibretroInput::Update()
{
  s_input_poll();

  for (u32 port = 0; port < PORT_COUNT; port++)
  {
    Controller* controller = Pad::GetController(port);
    if (!controller)
      continue;

    switch (controller->GetType())
    {
      case ControllerType::NeGcon:
        UpdateNeGcon(port, controller);
        break;

      case ControllerType::GunCon:
      {
        static constexpr std::array<std::pair<unsigned, u32>, 2> buttons = {{
          {RETRO_DEVICE_ID_LIGHTGUN_AUX_A, static_cast<u32>(GunCon::Binding::A)},
          {RETRO_DEVICE_ID_LIGHTGUN_AUX_B, static_cast<u32>(GunCon::Binding::B)},
        }};
        UpdateLightGun(port, controller, static_cast<u32>(GunCon::Binding::Trigger),
                       static_cast<u32>(GunCon::Binding::ShootOffscreen), buttons);
      }
      break;

      case ControllerType::Justifier:
      {
        static constexpr std::array<std::pair<unsigned, u32>, 2> buttons = {{
          {RETRO_DEVICE_ID_LIGHTGUN_START, static_cast<u32>(Justifier::Binding::Start)},
          {RETRO_DEVICE_ID_LIGHTGUN_AUX_A, static_cast<u32>(Justifier::Binding::Back)},
        }};
        UpdateLightGun(port, controller, static_cast<u32>(Justifier::Binding::Trigger),
                       static_cast<u32>(Justifier::Binding::ShootOffscreen), buttons);
      }
      break;

      case ControllerType::PlayStationMouse:
        UpdateMouse(port, controller);
        break;

      default:
        UpdatePad(port, controller);
        break;
    }
  }
}
