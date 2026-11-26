// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "common/types.h"

#include <string_view>

struct ImDrawList;
struct ImRect;
struct ImVec4;

namespace GameList {
struct Entry;
}

/// Generated cover cards for games without cover art.
namespace GameCard {
/// Colour picked from the game's serial (or title), the same on every run.
ImVec4 GetAccentColor(const GameList::Entry* entry);

/// The title without trailing tags such as "(USA)", keeping disc numbers.
std::string_view GetDisplayTitle(std::string_view title);

void Draw(ImDrawList* dl, const ImRect& rect, const GameList::Entry* entry, float rounding);
} // namespace GameCard
