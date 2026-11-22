// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "common/types.h"

class Error;
class StateWrapper;
struct Settings;

namespace ParallelPort {

bool Initialize(Error* error);
void Shutdown();
void Reset();
void UpdateSettings(const Settings& old_settings);

bool DoState(StateWrapper& sw);
size_t GetMaxStateSize();

u8 ReadByte(u32 offset);
void WriteByte(u32 offset, u8 value);

void ReadCode(u32 offset, u32* words, u32 word_count);

} // namespace ParallelPort
