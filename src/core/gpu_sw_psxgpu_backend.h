// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "gpu_sw_backend.h"

#include <array>
#include <vector>

struct rh_psx_gpu;

class GPU_PSXGPU_Backend final : public GPU_SW_Backend
{
public:
  GPU_PSXGPU_Backend();
  ~GPU_PSXGPU_Backend() override;

  bool Initialize(bool force_thread) override;
  void Reset() override;
  void Shutdown() override;

protected:
  void FillVRAM(u32 x, u32 y, u32 width, u32 height, u32 color, GPUBackendCommandParameters params) override;
  void UpdateVRAM(u32 x, u32 y, u32 width, u32 height, const void* data, GPUBackendCommandParameters params) override;
  void CopyVRAM(u32 src_x, u32 src_y, u32 dst_x, u32 dst_y, u32 width, u32 height,
                GPUBackendCommandParameters params) override;

  void DrawPolygon(const GPUBackendDrawPolygonCommand* cmd) override;
  void DrawLine(const GPUBackendDrawLineCommand* cmd) override;
  void DrawRectangle(const GPUBackendDrawRectangleCommand* cmd) override;
  void FlushRender() override;

private:
  void InvalidateTextureCache(u32 x, u32 y, u32 width, u32 height);
  void SetEnvironment(const GPUBackendDrawCommand* cmd, s32 offset_x, s32 offset_y);
  void PushEnvironmentWord(u32 index, u32 value);
  void SubmitPacket();

  rh_psx_gpu* m_psx_gpu = nullptr;
  std::array<u32, 8> m_ex_regs = {};
  u8 m_interlace_mode = 0;
  std::vector<u32> m_packet;
};
