// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "controller_auto_setup.h"
#include "controller.h"
#include "host.h"
#include "settings.h"
#include "types.h"

#include "common/memory_settings_interface.h"

#include <array>
#include <string_view>
#include <vector>

namespace ControllerAutoSetup {
static constexpr const char* PORT_SECTION = "Pad1";

static u16 ControllerBit(ControllerType type);
static bool IsGamepadBinding(const std::vector<std::string>& bindings);
static void CopyBinding(const SettingsInterface& base, SettingsInterface* profile, const char* from, const char* to);
static void SetupGunCon(const SettingsInterface& base, SettingsInterface* profile, std::string* message);
static void SetupAnalogController(const SettingsInterface& base, SettingsInterface* profile, std::string* message);
} // namespace ControllerAutoSetup

u16 ControllerAutoSetup::ControllerBit(ControllerType type)
{
  return static_cast<u16>(1u << static_cast<u32>(type));
}

bool ControllerAutoSetup::IsGamepadBinding(const std::vector<std::string>& bindings)
{
  for (const std::string& binding : bindings)
  {
    const std::string_view view(binding);
    if (!view.starts_with("Keyboard") && !view.starts_with("Pointer"))
      return true;
  }

  return false;
}

void ControllerAutoSetup::CopyBinding(const SettingsInterface& base, SettingsInterface* profile, const char* from,
                                      const char* to)
{
  const std::vector<std::string> bindings = base.GetStringList(PORT_SECTION, from);
  if (!bindings.empty())
    profile->SetStringList(PORT_SECTION, to, bindings);
}

void ControllerAutoSetup::SetupGunCon(const SettingsInterface& base, SettingsInterface* profile, std::string* message)
{
  profile->ClearSection(PORT_SECTION);
  profile->SetStringValue(PORT_SECTION, "Type", Controller::GetControllerInfo(ControllerType::GunCon)->name);
  CopyBinding(base, profile, "Start", "A");
  CopyBinding(base, profile, "Cross", "B");

  if (IsGamepadBinding(base.GetStringList(PORT_SECTION, "Cross")))
  {
    CopyBinding(base, profile, "R2", "Trigger");
    CopyBinding(base, profile, "L2", "ShootOffscreen");
    CopyBinding(base, profile, "LLeft", "RelativeLeft");
    CopyBinding(base, profile, "LRight", "RelativeRight");
    CopyBinding(base, profile, "LUp", "RelativeUp");
    CopyBinding(base, profile, "LDown", "RelativeDown");
    *message = TRANSLATE_STR("ControllerAutoSetup",
                             "This game needs a light gun, so port 1 is a GunCon: the left stick aims, R2 fires, L2 "
                             "shoots off-screen to reload, Start is A and Cross is B.");
  }
  else
  {
    profile->SetStringValue(PORT_SECTION, "Trigger", "Pointer-0/LeftButton");
    profile->SetStringValue(PORT_SECTION, "ShootOffscreen", "Pointer-0/RightButton");
    *message = TRANSLATE_STR("ControllerAutoSetup",
                             "This game needs a light gun, so port 1 is a GunCon: the mouse aims, the left button "
                             "fires and the right button reloads.");
  }
}

void ControllerAutoSetup::SetupAnalogController(const SettingsInterface& base, SettingsInterface* profile,
                                                std::string* message)
{
  profile->SetStringValue(PORT_SECTION, "Type", Controller::GetControllerInfo(ControllerType::AnalogController)->name);

  const std::vector<std::string> up_bindings = base.GetStringList(PORT_SECTION, "Up");
  if (!base.ContainsValue(PORT_SECTION, "LLeft") && IsGamepadBinding(up_bindings))
  {
    const std::string_view up(up_bindings.front());
    const std::string device(up.substr(0, up.find('/')));
    static constexpr std::array<std::pair<const char*, const char*>, 4> stick = {{
      {"LLeft", "-LeftX"},
      {"LRight", "+LeftX"},
      {"LUp", "-LeftY"},
      {"LDown", "+LeftY"},
    }};
    for (const auto& [key, axis] : stick)
      profile->SetStringValue(PORT_SECTION, key, fmt::format("{}/{}", device, axis).c_str());
  }

  *message =
    TRANSLATE_STR("ControllerAutoSetup", "This game needs analog sticks, so port 1 is a DualShock.");
}

std::unique_ptr<SettingsInterface>
ControllerAutoSetup::CreateInputProfile(const SettingsInterface& base, u16 supported_controllers, std::string* message)
{
  if (supported_controllers == 0 || supported_controllers == static_cast<u16>(-1))
    return nullptr;

  const Controller::ControllerInfo* cinfo = Controller::GetControllerInfo(base.GetStringValue(
    PORT_SECTION, "Type", Controller::GetControllerInfo(Settings::DEFAULT_CONTROLLER_1_TYPE)->name));
  const ControllerType type = cinfo ? cinfo->type : Settings::DEFAULT_CONTROLLER_1_TYPE;
  if (type == ControllerType::None || (supported_controllers & ControllerBit(type)) ||
      (type == ControllerType::AnalogController &&
       (supported_controllers & ControllerBit(ControllerType::DigitalController))))
  {
    return nullptr;
  }

  std::unique_ptr<SettingsInterface> profile = std::make_unique<MemorySettingsInterface>();
  profile->SetKeyValueList("ControllerPorts", base.GetKeyValueList("ControllerPorts"));
  for (u32 port = 0; port < NUM_CONTROLLER_AND_CARD_PORTS; port++)
  {
    const std::string section = Controller::GetSettingsSection(port);
    profile->SetKeyValueList(section.c_str(), base.GetKeyValueList(section.c_str()));
  }

  if (supported_controllers & ControllerBit(ControllerType::GunCon))
    SetupGunCon(base, profile.get(), message);
  else if ((supported_controllers & ControllerBit(ControllerType::AnalogController)) &&
           type == ControllerType::DigitalController)
    SetupAnalogController(base, profile.get(), message);
  else
    return nullptr;

  return profile;
}
