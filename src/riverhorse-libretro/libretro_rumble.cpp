// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "libretro_rumble.h"

#include "core/types.h"

#include "util/input_manager.h"
#include "util/input_source.h"

#include <algorithm>
#include <memory>

namespace LibretroRumble {
namespace {

class RumbleSource final : public InputSource
{
public:
  explicit RumbleSource(retro_set_rumble_state_t set_rumble_state) : m_set_rumble_state(set_rumble_state) {}

  bool Initialize(SettingsInterface& si, std::unique_lock<std::mutex>& settings_lock) override { return true; }
  void UpdateSettings(SettingsInterface& si, std::unique_lock<std::mutex>& settings_lock) override {}
  bool ReloadDevices() override { return false; }
  void Shutdown() override {}
  void PollEvents() override {}

  std::optional<InputBindingKey> ParseKeyString(std::string_view device, std::string_view binding) override
  {
    return std::nullopt;
  }
  TinyString ConvertKeyToString(InputBindingKey key) override { return {}; }
  TinyString ConvertKeyToIcon(InputBindingKey key) override { return {}; }
  std::vector<std::pair<std::string, std::string>> EnumerateDevices() override { return {}; }
  std::vector<InputBindingKey> EnumerateMotors() override { return {}; }
  bool GetGenericBindingMapping(std::string_view device, GenericInputBindingMapping* mapping) override
  {
    return false;
  }

  void UpdateMotorState(InputBindingKey key, float intensity) override
  {
    const retro_rumble_effect effect = (key.data == 0) ? RETRO_RUMBLE_STRONG : RETRO_RUMBLE_WEAK;
    m_set_rumble_state(key.source_index, effect, static_cast<u16>(std::clamp(intensity, 0.0f, 1.0f) * 65535.0f));
  }

private:
  retro_set_rumble_state_t m_set_rumble_state;
};

} // namespace

static std::unique_ptr<RumbleSource> s_source;

} // namespace LibretroRumble

void LibretroRumble::Initialize(retro_environment_t environment)
{
  retro_rumble_interface rumble = {};
  if (environment(RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE, &rumble) && rumble.set_rumble_state)
    s_source = std::make_unique<RumbleSource>(rumble.set_rumble_state);
}

void LibretroRumble::Shutdown()
{
  s_source.reset();
}

void LibretroRumble::AddBindings()
{
  if (!s_source)
    return;

  for (u32 pad = 0; pad < NUM_CONTROLLER_AND_CARD_PORTS; pad++)
  {
    const InputBindingKey large_motor = InputSource::MakeGenericControllerMotorKey(InputSourceType::Keyboard, pad, 0);
    const InputBindingKey small_motor = InputSource::MakeGenericControllerMotorKey(InputSourceType::Keyboard, pad, 1);
    InputManager::AddVibrationBinding(pad, &large_motor, s_source.get(), &small_motor, s_source.get());
  }
}
