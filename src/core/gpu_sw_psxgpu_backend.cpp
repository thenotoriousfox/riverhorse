// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "gpu_sw_psxgpu_backend.h"

#include "riverhorse_psx_gpu.h"

#include <algorithm>

GPU_PSXGPU_Backend::GPU_PSXGPU_Backend() = default;

GPU_PSXGPU_Backend::~GPU_PSXGPU_Backend()
{
  rh_psx_gpu_destroy(m_psx_gpu);
}

bool GPU_PSXGPU_Backend::Initialize(bool force_thread)
{
  m_psx_gpu = rh_psx_gpu_create(g_vram);
  if (!m_psx_gpu)
    return false;

  m_ex_regs.fill(UINT32_MAX);
  m_interlace_mode = 0;
  return GPU_SW_Backend::Initialize(force_thread);
}

void GPU_PSXGPU_Backend::Reset()
{
  GPU_SW_Backend::Reset();
  m_ex_regs.fill(UINT32_MAX);
  rh_psx_gpu_set_interlace(m_psx_gpu, 0, 0);
  m_interlace_mode = 0;
  InvalidateTextureCache(0, 0, VRAM_WIDTH, VRAM_HEIGHT);
}

void GPU_PSXGPU_Backend::Shutdown()
{
  GPU_SW_Backend::Shutdown();
  rh_psx_gpu_destroy(m_psx_gpu);
  m_psx_gpu = nullptr;
}

void GPU_PSXGPU_Backend::InvalidateTextureCache(u32 x, u32 y, u32 width, u32 height)
{
  if (x + width > VRAM_WIDTH || y + height > VRAM_HEIGHT)
    rh_psx_gpu_invalidate_region(m_psx_gpu, 0, 0, VRAM_WIDTH - 1, VRAM_HEIGHT - 1);
  else
    rh_psx_gpu_invalidate_region(m_psx_gpu, x, y, x + width - 1, y + height - 1);
}

void GPU_PSXGPU_Backend::FillVRAM(u32 x, u32 y, u32 width, u32 height, u32 color, GPUBackendCommandParameters params)
{
  rh_psx_gpu_flush(m_psx_gpu);
  GPU_SW_Backend::FillVRAM(x, y, width, height, color, params);
  InvalidateTextureCache(x, y, width, height);
}

void GPU_PSXGPU_Backend::UpdateVRAM(u32 x, u32 y, u32 width, u32 height, const void* data,
                                    GPUBackendCommandParameters params)
{
  rh_psx_gpu_flush(m_psx_gpu);
  GPU_SW_Backend::UpdateVRAM(x, y, width, height, data, params);
  InvalidateTextureCache(x, y, width, height);
}

void GPU_PSXGPU_Backend::CopyVRAM(u32 src_x, u32 src_y, u32 dst_x, u32 dst_y, u32 width, u32 height,
                                  GPUBackendCommandParameters params)
{
  rh_psx_gpu_flush(m_psx_gpu);
  GPU_SW_Backend::CopyVRAM(src_x, src_y, dst_x, dst_y, width, height, params);
  InvalidateTextureCache(dst_x, dst_y, width, height);
}

void GPU_PSXGPU_Backend::FlushRender()
{
  rh_psx_gpu_flush(m_psx_gpu);
}

void GPU_PSXGPU_Backend::PushEnvironmentWord(u32 index, u32 value)
{
  if (index != 1 && m_ex_regs[index] == value)
    return;

  m_ex_regs[index] = value;
  m_packet.push_back(value);
}

void GPU_PSXGPU_Backend::SetEnvironment(const GPUBackendDrawCommand* cmd, s32 offset_x, s32 offset_y)
{
  const GPUTextureWindow& window = cmd->window;
  const u32 mask_x = (static_cast<u8>(~window.and_x) >> 3) & 0x1F;
  const u32 mask_y = (static_cast<u8>(~window.and_y) >> 3) & 0x1F;
  const u32 window_value =
    mask_x | (mask_y << 5) | (((window.or_x >> 3) & 0x1F) << 10) | (((window.or_y >> 3) & 0x1F) << 15);

  PushEnvironmentWord(1, (0xE1u << 24) | (cmd->draw_mode.bits & GPUDrawModeReg::MASK));
  PushEnvironmentWord(2, (0xE2u << 24) | window_value);
  PushEnvironmentWord(3, (0xE3u << 24) | (m_drawing_area.left & 0x3FF) | ((m_drawing_area.top & 0x1FF) << 10));
  PushEnvironmentWord(4, (0xE4u << 24) | (m_drawing_area.right & 0x3FF) | ((m_drawing_area.bottom & 0x1FF) << 10));
  PushEnvironmentWord(5, (0xE5u << 24) | (static_cast<u32>(offset_x) & 0x7FF) |
                           ((static_cast<u32>(offset_y) & 0x7FF) << 11));
  PushEnvironmentWord(6, (0xE6u << 24) | BoolToUInt32(cmd->params.set_mask_while_drawing) |
                           (BoolToUInt32(cmd->params.check_mask_before_draw) << 1));

  const u8 interlace_mode = static_cast<u8>(BoolToUInt8(cmd->params.interlaced_rendering) |
                                            (BoolToUInt8(cmd->params.active_line_lsb == 0) << 1));
  if (interlace_mode != m_interlace_mode)
  {
    rh_psx_gpu_flush(m_psx_gpu);
    rh_psx_gpu_set_interlace(m_psx_gpu, cmd->params.interlaced_rendering, cmd->params.active_line_lsb == 0);
    m_interlace_mode = interlace_mode;
  }
}

void GPU_PSXGPU_Backend::SubmitPacket()
{
  rh_psx_gpu_parse(m_psx_gpu, m_packet.data(), static_cast<u32>(m_packet.size()), m_ex_regs.data());
  m_packet.clear();
}

static s32 GetPacketOffset(s32 min_pos, s32 max_pos)
{
  return std::clamp((min_pos + max_pos) / 2, -1024, 1023);
}

static u32 PackPosition(s32 x, s32 y, s32 offset_x, s32 offset_y)
{
  return (static_cast<u32>(x - offset_x) & 0xFFFFu) | (static_cast<u32>(y - offset_y) << 16);
}

void GPU_PSXGPU_Backend::DrawPolygon(const GPUBackendDrawPolygonCommand* cmd)
{
  const GPURenderCommand rc{cmd->rc.bits};
  const u32 num_vertices = rc.quad_polygon ? 4 : 3;

  s32 min_x = cmd->vertices[0].x, max_x = min_x, min_y = cmd->vertices[0].y, max_y = min_y;
  for (u32 i = 1; i < num_vertices; i++)
  {
    min_x = std::min(min_x, cmd->vertices[i].x);
    max_x = std::max(max_x, cmd->vertices[i].x);
    min_y = std::min(min_y, cmd->vertices[i].y);
    max_y = std::max(max_y, cmd->vertices[i].y);
  }
  const s32 offset_x = GetPacketOffset(min_x, max_x);
  const s32 offset_y = GetPacketOffset(min_y, max_y);

  SetEnvironment(cmd, offset_x, offset_y);

  const u32 texpage = cmd->draw_mode.bits & GPUDrawModeReg::POLYGON_TEXPAGE_MASK;
  m_packet.push_back((rc.bits & 0xFF000000u) | (cmd->vertices[0].color & 0x00FFFFFFu));
  for (u32 i = 0; i < num_vertices; i++)
  {
    const GPUBackendDrawPolygonCommand::Vertex& vert = cmd->vertices[i];
    if (rc.shading_enable && i > 0)
      m_packet.push_back(vert.color & 0x00FFFFFFu);
    m_packet.push_back(PackPosition(vert.x, vert.y, offset_x, offset_y));
    if (rc.texture_enable)
    {
      const u32 attribute = (i == 0) ? cmd->palette.bits : ((i == 1) ? texpage : 0u);
      m_packet.push_back((attribute << 16) | vert.texcoord);
    }
  }

  SubmitPacket();
}

void GPU_PSXGPU_Backend::DrawRectangle(const GPUBackendDrawRectangleCommand* cmd)
{
  const GPURenderCommand rc{cmd->rc.bits};
  SetEnvironment(cmd, 0, 0);

  static constexpr u32 RECTANGLE_SIZE_BITS = 0x18000000u;
  m_packet.push_back((rc.bits & 0xFF000000u & ~RECTANGLE_SIZE_BITS) | (cmd->color & 0x00FFFFFFu));
  m_packet.push_back(PackPosition(cmd->x, cmd->y, 0, 0));
  if (rc.texture_enable)
    m_packet.push_back((static_cast<u32>(cmd->palette.bits) << 16) | cmd->texcoord);
  m_packet.push_back(ZeroExtend32(cmd->width) | (ZeroExtend32(cmd->height) << 16));

  SubmitPacket();
}

void GPU_PSXGPU_Backend::DrawLine(const GPUBackendDrawLineCommand* cmd)
{
  const GPURenderCommand rc{cmd->rc.bits};
  static constexpr u32 POLYLINE_BIT = 0x08000000u;
  const u32 command = rc.bits & 0xFF000000u & ~POLYLINE_BIT;

  for (u16 i = 1; i < cmd->num_vertices; i++)
  {
    const GPUBackendDrawLineCommand::Vertex& start = cmd->vertices[i - 1];
    const GPUBackendDrawLineCommand::Vertex& end = cmd->vertices[i];
    const s32 offset_x = GetPacketOffset(std::min(start.x, end.x), std::max(start.x, end.x));
    const s32 offset_y = GetPacketOffset(std::min(start.y, end.y), std::max(start.y, end.y));
    SetEnvironment(cmd, offset_x, offset_y);

    m_packet.push_back(command | (start.color & 0x00FFFFFFu));
    m_packet.push_back(PackPosition(start.x, start.y, offset_x, offset_y));
    if (rc.shading_enable)
      m_packet.push_back(end.color & 0x00FFFFFFu);
    m_packet.push_back(PackPosition(end.x, end.y, offset_x, offset_y));
    SubmitPacket();
  }
}
