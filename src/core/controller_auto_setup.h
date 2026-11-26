// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "common/types.h"

#include <memory>
#include <string>

class SettingsInterface;

namespace ControllerAutoSetup {
std::unique_ptr<SettingsInterface> CreateInputProfile(const SettingsInterface& base, u16 supported_controllers,
                                                      std::string* message);
} // namespace ControllerAutoSetup
