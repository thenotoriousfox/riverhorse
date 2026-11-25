// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "libretro_messages.h"

#include "core/types.h"

#include "util/imgui_manager.h"

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace LibretroMessages {
namespace {
struct PendingMessage
{
  std::string text;
  float duration;
};
} // namespace

static constexpr float FRAMES_PER_SECOND_FOR_LEGACY_MESSAGES = 60.0f;

static retro_environment_t s_environment;
static unsigned s_message_interface_version = 0;
static std::mutex s_lock;
static std::vector<PendingMessage> s_pending;
static std::unordered_map<std::string, std::string> s_last_keyed_messages;

static void QueueMessage(std::string_view key, std::string_view message, float duration)
{
  std::unique_lock lock(s_lock);
  if (!key.empty())
  {
    std::string& last = s_last_keyed_messages[std::string(key)];
    if (last == message)
      return;
    last = message;
  }

  s_pending.push_back({std::string(message), duration});
}

void Initialize(retro_environment_t environment)
{
  s_environment = environment;
  if (!environment(RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION, &s_message_interface_version))
    s_message_interface_version = 0;

  ImGuiManager::SetOSDMessageSink(&QueueMessage);
}

void Shutdown()
{
  ImGuiManager::SetOSDMessageSink(nullptr);
  std::unique_lock lock(s_lock);
  s_pending.clear();
  s_last_keyed_messages.clear();
}

void Flush(bool show)
{
  std::vector<PendingMessage> messages;
  {
    std::unique_lock lock(s_lock);
    messages.swap(s_pending);
  }

  if (!show)
    return;

  for (const PendingMessage& message : messages)
  {
    if (s_message_interface_version >= 1)
    {
      retro_message_ext ext = {};
      ext.msg = message.text.c_str();
      ext.duration = static_cast<unsigned>(message.duration * 1000.0f);
      ext.priority = 1;
      ext.level = RETRO_LOG_INFO;
      ext.target = RETRO_MESSAGE_TARGET_OSD;
      ext.type = RETRO_MESSAGE_TYPE_NOTIFICATION;
      ext.progress = -1;
      s_environment(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &ext);
    }
    else
    {
      retro_message legacy = {message.text.c_str(),
                              static_cast<unsigned>(message.duration * FRAMES_PER_SECOND_FOR_LEGACY_MESSAGES)};
      s_environment(RETRO_ENVIRONMENT_SET_MESSAGE, &legacy);
    }
  }
}

} // namespace LibretroMessages
