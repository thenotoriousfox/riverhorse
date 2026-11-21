// SPDX-FileCopyrightText: 2019-2024 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only

#include "gpu.h"
#include "cpu_pgxp.h"
#include "gpu_backend.h"
#include "interrupt_controller.h"
#include "system.h"
#include "texture_replacements.h"

#include "common/assert.h"
#include "common/log.h"
#include "common/string_util.h"

Log_SetChannel(GPU);

#define CHECK_COMMAND_SIZE(num_words)                                                                                  \
  if (m_fifo.GetSize() < num_words)                                                                                    \
  {                                                                                                                    \
    m_command_total_words = num_words;                                                                                 \
    return false;                                                                                                      \
  }

static u32 s_cpu_to_vram_dump_id = 1;
static u32 s_vram_to_cpu_dump_id = 1;

static constexpr u32 ReplaceZero(u32 value, u32 value_for_zero)
{
  return value == 0 ? value_for_zero : value;
}

void GPU::TryExecuteCommands()
{
  while (m_pending_command_ticks <= m_max_run_ahead && !m_fifo.IsEmpty())
  {
    switch (m_blitter_state)
    {
      case BlitterState::Idle:
      {
        const u32 command = FifoPeek(0) >> 24;
        if ((this->*s_GP0_command_handler_table[command])())
          continue;
        else
          return;
      }

      case BlitterState::WritingVRAM:
      {
        DebugAssert(m_blit_remaining_words > 0);
        const u32 words_to_copy = std::min(m_blit_remaining_words, m_fifo.GetSize());
        m_blit_buffer.reserve(m_blit_buffer.size() + words_to_copy);
        for (u32 i = 0; i < words_to_copy; i++)
          m_blit_buffer.push_back(FifoPop());
        m_blit_remaining_words -= words_to_copy;

        DEBUG_LOG("VRAM write burst of {} words, {} words remaining", words_to_copy, m_blit_remaining_words);
        if (m_blit_remaining_words == 0)
          FinishVRAMWrite();

        continue;
      }

      case BlitterState::ReadingVRAM:
      {
        return;
      }
      break;

      case BlitterState::DrawingPolyLine:
      {
        const u32 words_per_vertex = m_render_command.shading_enable ? 2 : 1;
        u32 terminator_index =
          m_render_command.shading_enable ? ((static_cast<u32>(m_polyline_buffer.size()) & 1u) ^ 1u) : 0u;
        for (; terminator_index < m_fifo.GetSize(); terminator_index += words_per_vertex)
        {
          // polyline must have at least two vertices, and the terminator is (word & 0xf000f000) == 0x50005000.
          // terminator is on the first word for the vertex
          if ((FifoPeek(terminator_index) & UINT32_C(0xF000F000)) == UINT32_C(0x50005000))
            break;
        }

        const bool found_terminator = (terminator_index < m_fifo.GetSize());
        const u32 words_to_copy = std::min(terminator_index, m_fifo.GetSize());
        if (words_to_copy > 0)
        {
          m_polyline_buffer.reserve(m_polyline_buffer.size() + words_to_copy);
          for (u32 i = 0; i < words_to_copy; i++)
            m_polyline_buffer.push_back(m_fifo.Pop());
        }

        DEBUG_LOG("Added {} words to polyline", words_to_copy);
        if (found_terminator)
        {
          // drop terminator
          m_fifo.RemoveOne();
          DEBUG_LOG("Drawing poly-line with {} vertices", GetPolyLineVertexCount());
          DispatchRenderCommand();
          m_polyline_buffer.clear();
          EndCommand();
          continue;
        }

        if (GetPolyLineVertexCount() >= MAX_POLYLINE_VERTICES)
          DrawBufferedPolyLineSegment();
      }
      break;
    }
  }
}

void GPU::ExecuteCommands()
{
  const bool was_executing_from_event = std::exchange(m_executing_commands, true);

  TryExecuteCommands();
  UpdateDMARequest();
  UpdateGPUIdle();

  m_executing_commands = was_executing_from_event;
  if (!was_executing_from_event)
    UpdateCommandTickEvent();
}

void GPU::DrawBufferedPolyLineSegment()
{
  DispatchRenderCommand();

  const size_t count = m_polyline_buffer.size();
  if (!m_render_command.shading_enable)
  {
    const u64 last_position = m_polyline_buffer[count - 1];
    m_polyline_buffer.clear();
    m_polyline_buffer.push_back(last_position);
    return;
  }

  const bool pending_color = (count & 1u) == 0;
  const size_t last_position_index = pending_color ? (count - 2) : (count - 1);
  const u64 last_position = m_polyline_buffer[last_position_index];
  const u64 pending = m_polyline_buffer[count - 1];
  m_render_command.color_for_first_vertex =
    Truncate32(m_polyline_buffer[last_position_index - 1]) & UINT32_C(0x00FFFFFF);
  m_polyline_buffer.clear();
  m_polyline_buffer.push_back(last_position);
  if (pending_color)
    m_polyline_buffer.push_back(pending);
}

void GPU::EndCommand()
{
  m_blitter_state = BlitterState::Idle;
  m_command_total_words = 0;
}

u32 GPU::GetGP0CommandWordCount(u32 command)
{
  const u32 textured = BoolToUInt32((command & 0x04) != 0);
  const bool quad_or_polyline = ((command & 0x08) != 0);
  const u32 shaded = BoolToUInt32((command & 0x10) != 0);
  switch (command >> 5)
  {
    case 0:
      return (command == 0x02) ? 3 : 1;

    case 1:
      return (1 + textured + shaded) * (quad_or_polyline ? 4 : 3) + (shaded ^ 1);

    case 2:
      return 3 + shaded;

    case 3:
      return 2 + textured + BoolToUInt32(((command >> 3) & 0x03) == 0);

    case 4:
      return 4;

    case 5:
    case 6:
      return 3;

    default:
      return 1;
  }
}

u32 GPU::GetGP0CommandHeldWords(u32 command)
{
  if ((command >> 5) != 1)
    return 0;

  static constexpr std::array<u8, 4> quad_held_words = {5, 8, 7, 10};
  const u32 textured = BoolToUInt32((command & 0x04) != 0);
  const u32 shaded = BoolToUInt32((command & 0x10) != 0);
  return ((command & 0x08) != 0) ? quad_held_words[textured | (shaded << 1)] : GetGP0CommandWordCount(command);
}

GPU::GP0CommandHandlerTable GPU::GenerateGP0CommandHandlerTable()
{
  GP0CommandHandlerTable table = {};
  for (u32 i = 0; i < static_cast<u32>(table.size()); i++)
    table[i] = &GPU::HandleUnknownGP0Command;
  table[0x00] = &GPU::HandleNOPCommand;
  table[0x01] = &GPU::HandleClearCacheCommand;
  table[0x02] = &GPU::HandleFillRectangleCommand;
  table[0x03] = &GPU::HandleNOPCommand;
  for (u32 i = 0x04; i <= 0x1E; i++)
    table[i] = &GPU::HandleNOPCommand;
  table[0x1F] = &GPU::HandleInterruptRequestCommand;
  for (u32 i = 0x20; i <= 0x7F; i++)
  {
    const GPURenderCommand rc{i << 24};
    switch (rc.primitive)
    {
      case GPUPrimitive::Polygon:
        table[i] = &GPU::HandleRenderPolygonCommand;
        break;
      case GPUPrimitive::Line:
        table[i] = rc.polyline ? &GPU::HandleRenderPolyLineCommand : &GPU::HandleRenderLineCommand;
        break;
      case GPUPrimitive::Rectangle:
        table[i] = &GPU::HandleRenderRectangleCommand;
        break;
      default:
        table[i] = &GPU::HandleUnknownGP0Command;
        break;
    }
  }
  table[0xE0] = &GPU::HandleNOPCommand;
  table[0xE1] = &GPU::HandleSetDrawModeCommand;
  table[0xE2] = &GPU::HandleSetTextureWindowCommand;
  table[0xE3] = &GPU::HandleSetDrawingAreaTopLeftCommand;
  table[0xE4] = &GPU::HandleSetDrawingAreaBottomRightCommand;
  table[0xE5] = &GPU::HandleSetDrawingOffsetCommand;
  table[0xE6] = &GPU::HandleSetMaskBitCommand;
  for (u32 i = 0xE7; i <= 0xEF; i++)
    table[i] = &GPU::HandleNOPCommand;
  for (u32 i = 0x80; i <= 0x9F; i++)
    table[i] = &GPU::HandleCopyRectangleVRAMToVRAMCommand;
  for (u32 i = 0xA0; i <= 0xBF; i++)
    table[i] = &GPU::HandleCopyRectangleCPUToVRAMCommand;
  for (u32 i = 0xC0; i <= 0xDF; i++)
    table[i] = &GPU::HandleCopyRectangleVRAMToCPUCommand;

  table[0xFF] = &GPU::HandleNOPCommand;

  return table;
}

bool GPU::HandleUnknownGP0Command()
{
  const u32 command = FifoPeek() >> 24;
  ERROR_LOG("Unimplemented GP0 command 0x{:02X}", command);

  SmallString dump;
  for (u32 i = 0; i < m_fifo.GetSize(); i++)
    dump.append_format("{}{:08X}", (i > 0) ? " " : "", FifoPeek(i));
  ERROR_LOG("FIFO: {}", dump);

  m_fifo.RemoveOne();
  EndCommand();
  return true;
}

bool GPU::HandleNOPCommand()
{
  m_fifo.RemoveOne();
  EndCommand();
  return true;
}

bool GPU::HandleClearCacheCommand()
{
  DEBUG_LOG("GP0 clear cache");
  m_draw_mode.SetTexturePageChanged();
  InvalidateCLUT();
  m_fifo.RemoveOne();
  AddCommandTicks(1);
  EndCommand();
  return true;
}

bool GPU::HandleInterruptRequestCommand()
{
  DEBUG_LOG("GP0 interrupt request");

  m_GPUSTAT.interrupt_request = true;
  InterruptController::SetLineState(InterruptController::IRQ::GPU, true);

  m_fifo.RemoveOne();
  AddCommandTicks(1);
  EndCommand();
  return true;
}

bool GPU::HandleSetDrawModeCommand()
{
  const u32 param = FifoPop() & 0x00FFFFFFu;
  DEBUG_LOG("Set draw mode {:08X}", param);
  SetDrawMode(Truncate16(param));
  AddCommandTicks(1);
  EndCommand();
  return true;
}

bool GPU::HandleSetTextureWindowCommand()
{
  const u32 param = FifoPop() & 0x00FFFFFFu;
  SetTextureWindow(param);
  AddCommandTicks(1);
  EndCommand();
  return true;
}

bool GPU::HandleSetDrawingAreaTopLeftCommand()
{
  const u32 param = FifoPop() & 0x00FFFFFFu;
  const u32 left = param & DRAWING_AREA_COORD_MASK;
  const u32 top = (param >> 10) & DRAWING_AREA_COORD_MASK;
  DEBUG_LOG("Set drawing area top-left: ({}, {})", left, top);
  if (m_drawing_area.left != left || m_drawing_area.top != top)
  {
    FlushRender();

    m_drawing_area.left = left;
    m_drawing_area.top = top;
    m_drawing_area_changed = true;
    SetClampedDrawingArea();
  }

  AddCommandTicks(1);
  EndCommand();
  return true;
}

bool GPU::HandleSetDrawingAreaBottomRightCommand()
{
  const u32 param = FifoPop() & 0x00FFFFFFu;

  const u32 right = param & DRAWING_AREA_COORD_MASK;
  const u32 bottom = (param >> 10) & DRAWING_AREA_COORD_MASK;
  DEBUG_LOG("Set drawing area bottom-right: ({}, {})", m_drawing_area.right, m_drawing_area.bottom);
  if (m_drawing_area.right != right || m_drawing_area.bottom != bottom)
  {
    FlushRender();

    m_drawing_area.right = right;
    m_drawing_area.bottom = bottom;
    m_drawing_area_changed = true;
    SetClampedDrawingArea();
  }

  AddCommandTicks(1);
  EndCommand();
  return true;
}

bool GPU::HandleSetDrawingOffsetCommand()
{
  const u32 param = FifoPop() & 0x00FFFFFFu;
  const s32 x = SignExtendN<11, s32>(param & 0x7FFu);
  const s32 y = SignExtendN<11, s32>((param >> 11) & 0x7FFu);
  DEBUG_LOG("Set drawing offset ({}, {})", m_drawing_offset.x, m_drawing_offset.y);
  if (m_drawing_offset.x != x || m_drawing_offset.y != y)
  {
    FlushRender();

    m_drawing_offset.x = x;
    m_drawing_offset.y = y;
  }

  AddCommandTicks(1);
  EndCommand();
  return true;
}

bool GPU::HandleSetMaskBitCommand()
{
  const u32 param = FifoPop() & 0x00FFFFFFu;

  constexpr u32 gpustat_mask = (1 << 11) | (1 << 12);
  const u32 gpustat_bits = (param & 0x03) << 11;
  if ((m_GPUSTAT.bits & gpustat_mask) != gpustat_bits)
  {
    FlushRender();
    m_GPUSTAT.bits = (m_GPUSTAT.bits & ~gpustat_mask) | gpustat_bits;
  }
  DEBUG_LOG("Set mask bit {} {}", BoolToUInt32(m_GPUSTAT.set_mask_while_drawing),
            BoolToUInt32(m_GPUSTAT.check_mask_before_draw));

  AddCommandTicks(1);
  EndCommand();
  return true;
}

bool GPU::HandleRenderPolygonCommand()
{
  const GPURenderCommand rc{FifoPeek(0)};

  // shaded vertices use the colour from the first word for the first vertex
  const u32 words_per_vertex = 1 + BoolToUInt32(rc.texture_enable) + BoolToUInt32(rc.shading_enable);
  const u32 num_vertices = rc.quad_polygon ? 4 : 3;
  const u32 total_words = words_per_vertex * num_vertices + BoolToUInt32(!rc.shading_enable);
  CHECK_COMMAND_SIZE(total_words);

  if (IsInterlacedRenderingEnabled() && IsCRTCScanlinePending())
    SynchronizeCRTC();

  // setup time
  static constexpr u16 s_setup_time[2][2][2] = {{{50, 130}, {200, 295}}, {{50, 215}, {335, 495}}};
  const TickCount setup_ticks = static_cast<TickCount>(ZeroExtend32(
    s_setup_time[BoolToUInt8(rc.quad_polygon)][BoolToUInt8(rc.shading_enable)][BoolToUInt8(rc.texture_enable)]));
  m_dreq_hold_start_ticks = m_pending_command_ticks;
  AddCommandTicks(setup_ticks);
  HoldDMARequestThroughCurrentTicks();

  TRACE_LOG("Render {} {} {} {} polygon ({} verts, {} words per vert), {} setup ticks",
            rc.quad_polygon ? "four-point" : "three-point", rc.transparency_enable ? "semi-transparent" : "opaque",
            rc.texture_enable ? "textured" : "non-textured", rc.shading_enable ? "shaded" : "monochrome", num_vertices,
            words_per_vertex, setup_ticks);

  // set draw state up
  if (rc.texture_enable)
  {
    const u16 texpage_attribute = Truncate16((rc.shading_enable ? FifoPeek(5) : FifoPeek(4)) >> 16);
    SetDrawMode((texpage_attribute & GPUDrawModeReg::POLYGON_TEXPAGE_MASK) |
                (m_draw_mode.mode_reg.bits & ~GPUDrawModeReg::POLYGON_TEXPAGE_MASK));
    SetTexturePalette(Truncate16(FifoPeek(2) >> 16));
    UpdateCLUTIfNeeded(m_draw_mode.mode_reg.texture_mode, m_draw_mode.palette_reg);
  }

  m_counters.num_vertices += num_vertices;
  m_counters.num_primitives++;
  m_render_command.bits = rc.bits;
  m_fifo.RemoveOne();

  DispatchRenderCommand();
  EndCommand();
  return true;
}

bool GPU::HandleRenderRectangleCommand()
{
  const GPURenderCommand rc{FifoPeek(0)};
  const u32 total_words =
    2 + BoolToUInt32(rc.texture_enable) + BoolToUInt32(rc.rectangle_size == GPUDrawRectangleSize::Variable);

  CHECK_COMMAND_SIZE(total_words);

  if (IsInterlacedRenderingEnabled() && IsCRTCScanlinePending())
    SynchronizeCRTC();

  if (rc.texture_enable)
  {
    SetTexturePalette(Truncate16(FifoPeek(2) >> 16));
    UpdateCLUTIfNeeded(m_draw_mode.mode_reg.texture_mode, m_draw_mode.palette_reg);
  }

  const TickCount setup_ticks = 50;
  AddCommandTicks(setup_ticks);

  TRACE_LOG("Render {} {} {} rectangle ({} words), {} setup ticks",
            rc.transparency_enable ? "semi-transparent" : "opaque", rc.texture_enable ? "textured" : "non-textured",
            rc.shading_enable ? "shaded" : "monochrome", total_words, setup_ticks);

  m_counters.num_vertices++;
  m_counters.num_primitives++;
  m_render_command.bits = rc.bits;
  m_fifo.RemoveOne();

  DispatchRenderCommand();
  EndCommand();
  return true;
}

bool GPU::HandleRenderLineCommand()
{
  const GPURenderCommand rc{FifoPeek(0)};
  const u32 total_words = rc.shading_enable ? 4 : 3;
  CHECK_COMMAND_SIZE(total_words);

  if (IsInterlacedRenderingEnabled() && IsCRTCScanlinePending())
    SynchronizeCRTC();

  TRACE_LOG("Render {} {} line ({} total words)", rc.transparency_enable ? "semi-transparent" : "opaque",
            rc.shading_enable ? "shaded" : "monochrome", total_words);

  m_counters.num_vertices += 2;
  m_counters.num_primitives++;
  m_render_command.bits = rc.bits;
  m_fifo.RemoveOne();

  DispatchRenderCommand();
  EndCommand();
  return true;
}

bool GPU::HandleRenderPolyLineCommand()
{
  // always read the first two vertices, we test for the terminator after that
  const GPURenderCommand rc{FifoPeek(0)};
  const u32 min_words = rc.shading_enable ? 4 : 3;
  CHECK_COMMAND_SIZE(min_words);

  if (IsInterlacedRenderingEnabled() && IsCRTCScanlinePending())
    SynchronizeCRTC();

  const TickCount setup_ticks = 16;
  m_dreq_hold_start_ticks = m_pending_command_ticks;
  AddCommandTicks(setup_ticks);

  TRACE_LOG("Render {} {} poly-line, {} setup ticks", rc.transparency_enable ? "semi-transparent" : "opaque",
            rc.shading_enable ? "shaded" : "monochrome", setup_ticks);

  m_render_command.bits = rc.bits;
  m_fifo.RemoveOne();

  const u32 words_to_pop = min_words - 1;
  m_polyline_buffer.clear();
  m_polyline_buffer.reserve(words_to_pop);
  for (u32 i = 0; i < words_to_pop; i++)
    m_polyline_buffer.push_back(m_fifo.Pop());

  m_blitter_state = BlitterState::DrawingPolyLine;
  m_command_total_words = 0;
  return true;
}

bool GPU::HandleFillRectangleCommand()
{
  CHECK_COMMAND_SIZE(3);

  if (IsInterlacedRenderingEnabled() && IsCRTCScanlinePending())
    SynchronizeCRTC();

  FlushRender();

  const u32 color = FifoPop() & 0x00FFFFFF;
  const u32 dst_x = FifoPeek() & 0x3F0;
  const u32 dst_y = (FifoPop() >> 16) & VRAM_HEIGHT_MASK;
  const u32 width = ((FifoPeek() & VRAM_WIDTH_MASK) + 0xF) & ~0xF;
  const u32 height = (FifoPop() >> 16) & VRAM_HEIGHT_MASK;

  DEBUG_LOG("Fill VRAM rectangle offset=({},{}), size=({},{})", dst_x, dst_y, width, height);

  if (width > 0 && height > 0)
    FillVRAM(dst_x, dst_y, width, height, color);

  m_counters.num_writes++;
  AddCommandTicks(10 + 5 * height + (width * height) / 12);
  EndCommand();
  return true;
}

bool GPU::HandleCopyRectangleCPUToVRAMCommand()
{
  CHECK_COMMAND_SIZE(3);
  m_fifo.RemoveOne();

  const u32 coords = FifoPop();
  const u32 size = FifoPop();

  // Tenga Seiha does a bunch of completely-invalid VRAM writes on boot, then expects GPU idle to be set.
  // It's unclear what actually happens, I need to write another test, but for now, just skip these uploads.
  // Not setting GPU idle during the write command breaks Doom, so that's not an option.
  if (size == 0xFFFFFFFFu) [[unlikely]]
  {
    ERROR_LOG("Ignoring likely-invalid VRAM write to ({},{})", (coords & VRAM_WIDTH_MASK),
              ((coords >> 16) & VRAM_HEIGHT_MASK));
    return true;
  }

  const u32 dst_x = coords & VRAM_WIDTH_MASK;
  const u32 dst_y = (coords >> 16) & VRAM_HEIGHT_MASK;
  const u32 copy_width = ReplaceZero(size & VRAM_WIDTH_MASK, 0x400);
  const u32 copy_height = ReplaceZero((size >> 16) & VRAM_HEIGHT_MASK, 0x200);
  const u32 num_pixels = copy_width * copy_height;
  const u32 num_words = ((num_pixels + 1) / 2);

  DEBUG_LOG("Copy rectangle from CPU to VRAM offset=({},{}), size=({},{})", dst_x, dst_y, copy_width, copy_height);

  EndCommand();

  m_blitter_state = BlitterState::WritingVRAM;
  m_blit_buffer.reserve(num_words);
  m_blit_remaining_words = num_words;
  m_vram_transfer.x = Truncate16(dst_x);
  m_vram_transfer.y = Truncate16(dst_y);
  m_vram_transfer.width = Truncate16(copy_width);
  m_vram_transfer.height = Truncate16(copy_height);
  return true;
}

void GPU::FinishVRAMWrite(bool write_partial_row)
{
  if (IsInterlacedRenderingEnabled() && IsCRTCScanlinePending())
    SynchronizeCRTC();

  FlushRender();

  if (m_blit_remaining_words == 0)
  {
    if (g_settings.debugging.dump_cpu_to_vram_copies)
    {
      DumpVRAMToFile(TinyString::from_format("cpu_to_vram_copy_{}.png", s_cpu_to_vram_dump_id++), m_vram_transfer.width,
                     m_vram_transfer.height, sizeof(u16) * m_vram_transfer.width, m_blit_buffer.data(), true);
    }

    if (g_settings.texture_replacements.ShouldDumpVRAMWrite(m_vram_transfer.width, m_vram_transfer.height))
    {
      TextureReplacements::DumpVRAMWrite(m_vram_transfer.width, m_vram_transfer.height,
                                         reinterpret_cast<const u16*>(m_blit_buffer.data()));
    }

    UpdateVRAM(m_vram_transfer.x, m_vram_transfer.y, m_vram_transfer.width, m_vram_transfer.height,
               m_blit_buffer.data(), m_GPUSTAT.set_mask_while_drawing, m_GPUSTAT.check_mask_before_draw);
  }
  else
  {
    const u32 num_pixels = ZeroExtend32(m_vram_transfer.width) * ZeroExtend32(m_vram_transfer.height);
    const u32 num_words = (num_pixels + 1) / 2;
    const u32 transferred_words = num_words - m_blit_remaining_words;
    u32 transferred_pixels = transferred_words * 2;
    if (transferred_pixels > 0 && ((transferred_pixels - 1) % m_vram_transfer.width) == 0)
      transferred_pixels--;
    const u32 transferred_full_rows = transferred_pixels / m_vram_transfer.width;
    const u32 transferred_width_last_row = transferred_pixels % m_vram_transfer.width;

    WARNING_LOG("Partial VRAM write - transfer finished with {} of {} words remaining ({} full rows, {} last row)",
                m_blit_remaining_words, num_words, transferred_full_rows, transferred_width_last_row);

    const u8* blit_ptr = reinterpret_cast<const u8*>(m_blit_buffer.data());
    if (transferred_full_rows > 0)
    {
      UpdateVRAM(m_vram_transfer.x, m_vram_transfer.y, m_vram_transfer.width, transferred_full_rows, blit_ptr,
                 m_GPUSTAT.set_mask_while_drawing, m_GPUSTAT.check_mask_before_draw);
      blit_ptr += (ZeroExtend32(m_vram_transfer.width) * transferred_full_rows) * sizeof(u16);
    }
    if (write_partial_row && transferred_width_last_row > 0)
    {
      UpdateVRAM(m_vram_transfer.x, m_vram_transfer.y + transferred_full_rows, transferred_width_last_row, 1, blit_ptr,
                 m_GPUSTAT.set_mask_while_drawing, m_GPUSTAT.check_mask_before_draw);
    }
  }

  m_counters.num_writes++;
  m_blit_buffer.clear();
  m_vram_transfer = {};
  m_blitter_state = BlitterState::Idle;
}

bool GPU::HandleCopyRectangleVRAMToCPUCommand()
{
  CHECK_COMMAND_SIZE(3);
  m_fifo.RemoveOne();

  m_vram_transfer.x = Truncate16(FifoPeek() & VRAM_WIDTH_MASK);
  m_vram_transfer.y = Truncate16((FifoPop() >> 16) & VRAM_HEIGHT_MASK);
  m_vram_transfer.width = ((Truncate16(FifoPeek()) - 1) & VRAM_WIDTH_MASK) + 1;
  m_vram_transfer.height = ((Truncate16(FifoPop() >> 16) - 1) & VRAM_HEIGHT_MASK) + 1;

  DEBUG_LOG("Copy rectangle from VRAM to CPU offset=({},{}), size=({},{})", m_vram_transfer.x, m_vram_transfer.y,
            m_vram_transfer.width, m_vram_transfer.height);
  DebugAssert(m_vram_transfer.col == 0 && m_vram_transfer.row == 0);

  // all rendering should be done first...
  FlushRender();

  // ensure VRAM shadow is up to date
  const u16 read_width = m_vram_transfer.width + (m_vram_transfer.width & m_vram_transfer.height & 1u);
  ReadVRAM(m_vram_transfer.x, m_vram_transfer.y, read_width, m_vram_transfer.height);

  if (g_settings.debugging.dump_vram_to_cpu_copies)
  {
    DumpVRAMToFile(TinyString::from_format("vram_to_cpu_copy_{}.png", s_vram_to_cpu_dump_id++), m_vram_transfer.width,
                   m_vram_transfer.height, sizeof(u16) * VRAM_WIDTH,
                   &g_vram[m_vram_transfer.y * VRAM_WIDTH + m_vram_transfer.x], true);
  }

  // switch to pixel-by-pixel read state
  m_counters.num_reads++;
  m_blitter_state = BlitterState::ReadingVRAM;
  m_command_total_words = 0;
  return true;
}

bool GPU::HandleCopyRectangleVRAMToVRAMCommand()
{
  CHECK_COMMAND_SIZE(4);
  m_fifo.RemoveOne();

  const u32 src_x = FifoPeek() & VRAM_WIDTH_MASK;
  const u32 src_y = (FifoPop() >> 16) & VRAM_HEIGHT_MASK;
  const u32 dst_x = FifoPeek() & VRAM_WIDTH_MASK;
  const u32 dst_y = (FifoPop() >> 16) & VRAM_HEIGHT_MASK;
  const u32 width = ReplaceZero(FifoPeek() & VRAM_WIDTH_MASK, 0x400);
  const u32 height = ReplaceZero((FifoPop() >> 16) & VRAM_HEIGHT_MASK, 0x200);

  DEBUG_LOG("Copy rectangle from VRAM to VRAM src=({},{}), dst=({},{}), size=({},{})", src_x, src_y, dst_x, dst_y,
            width, height);

  // Some VRAM copies aren't going to do anything. Most games seem to send a 2x2 VRAM copy at the end of a frame.
  const bool skip_copy =
    width == 0 || height == 0 || (src_x == dst_x && src_y == dst_y && !m_GPUSTAT.set_mask_while_drawing);
  if (!skip_copy)
  {
    m_counters.num_copies++;

    FlushRender();
    CopyVRAM(src_x, src_y, dst_x, dst_y, width, height);
  }

  AddCommandTicks(50 + 21 * height * ((width + 127) / 128) + (width * height * 5) / 4);
  EndCommand();
  return true;
}

void GPU::FillBackendCommandParameters(GPUBackendCommand* cmd) const
{
  cmd->params.bits = 0;
  cmd->params.check_mask_before_draw = m_GPUSTAT.check_mask_before_draw;
  cmd->params.set_mask_while_drawing = m_GPUSTAT.set_mask_while_drawing;
  cmd->params.active_line_lsb = m_crtc_state.active_line_lsb;
  cmd->params.interlaced_rendering = IsInterlacedRenderingEnabled();
}

void GPU::FillDrawCommand(GPUBackendDrawCommand* cmd, GPURenderCommand rc)
{
  FillBackendCommandParameters(cmd);
  cmd->params.texture_page_changed = std::exchange(m_draw_mode.texture_page_changed, false);
  cmd->params.texture_window_changed = std::exchange(m_draw_mode.texture_window_changed, false);
  cmd->rc.bits = rc.bits;
  cmd->draw_mode.bits = m_draw_mode.mode_reg.bits;
  cmd->palette.bits = m_draw_mode.palette_reg.bits;
  cmd->window = m_draw_mode.texture_window;
}

bool GPU::AddPolygonTicks(const GSVector2i* positions, GPURenderCommand rc)
{
  const GSVector2i min_pos_12 = positions[1].min_i32(positions[2]);
  const GSVector2i max_pos_12 = positions[1].max_i32(positions[2]);
  const GSVector4i draw_rect_012 = GSVector4i(min_pos_12.min_i32(positions[0]))
                                     .upl64(GSVector4i(max_pos_12.max_i32(positions[0])))
                                     .add32(GSVector4i::cxpr(0, 0, 1, 1));
  const bool first_tri_culled =
    (draw_rect_012.width() > MAX_PRIMITIVE_WIDTH || draw_rect_012.height() > MAX_PRIMITIVE_HEIGHT ||
     !m_clamped_drawing_area.rintersects(draw_rect_012));
  if (first_tri_culled)
  {
    DEBUG_LOG("Culling off-screen/too-large polygon: {},{} {},{} {},{}", positions[0].x, positions[0].y, positions[1].x,
              positions[1].y, positions[2].x, positions[2].y);

    if (!rc.quad_polygon)
      return false;
  }
  else
  {
    AddDrawTriangleTicks(positions[0], positions[1], positions[2], rc.shading_enable, rc.texture_enable,
                         rc.transparency_enable);
  }

  if (!rc.quad_polygon)
    return true;

  HoldDMARequestThroughCurrentTicks();

  const GSVector4i draw_rect_123 = GSVector4i(min_pos_12.min_i32(positions[3]))
                                     .upl64(GSVector4i(max_pos_12.max_i32(positions[3])))
                                     .add32(GSVector4i::cxpr(0, 0, 1, 1));
  const bool second_tri_culled =
    (draw_rect_123.width() > MAX_PRIMITIVE_WIDTH || draw_rect_123.height() > MAX_PRIMITIVE_HEIGHT ||
     !m_clamped_drawing_area.rintersects(draw_rect_123));
  if (second_tri_culled)
  {
    DEBUG_LOG("Culling too-large polygon (quad second half): {},{} {},{} {},{}", positions[2].x, positions[2].y,
              positions[1].x, positions[1].y, positions[0].x, positions[0].y);

    return !first_tri_culled;
  }

  AddDrawTriangleTicks(positions[2], positions[1], positions[3], rc.shading_enable, rc.texture_enable,
                       rc.transparency_enable);
  return true;
}

bool GPU::AddLineSegmentTicks(GSVector2i start, GSVector2i end, GPURenderCommand rc, bool* out_visible)
{
  const GSVector4i v0 = GSVector4i(start);
  const GSVector4i v1 = GSVector4i(end);
  const GSVector4i rect = v0.min_i32(v1).xyxy(v0.max_i32(v1)).add32(GSVector4i::cxpr(0, 0, 1, 1));
  const GSVector4i clamped_rect = rect.rintersect(m_clamped_drawing_area);
  if (rect.width() > MAX_PRIMITIVE_WIDTH || rect.height() > MAX_PRIMITIVE_HEIGHT)
  {
    DEBUG_LOG("Culling too-large line: {},{} - {},{}", start.x, start.y, end.x, end.y);
    *out_visible = false;
    return false;
  }

  AddDrawLineTicks(clamped_rect, rc.shading_enable, rc.transparency_enable);
  *out_visible = !clamped_rect.rempty();
  return true;
}

void GPU::GetPreciseVertexPosition(u64 maddr_and_pos, s32 native_x, s32 native_y, float* x, float* y, float* w,
                                   bool* valid_w) const
{
  if (!g_settings.gpu_pgxp_enable)
  {
    *x = static_cast<float>(native_x);
    *y = static_cast<float>(native_y);
    *w = 1.0f;
    return;
  }

  *valid_w &= CPU::PGXP::GetPreciseVertex(Truncate32(maddr_and_pos >> 32), Truncate32(maddr_and_pos), native_x,
                                          native_y, m_drawing_offset.x, m_drawing_offset.y, x, y, w);
}

void GPU::DispatchRenderCommand()
{
  if (m_drawing_area_changed)
  {
    GPUBackendSetDrawingAreaCommand* cmd = m_backend->NewSetDrawingAreaCommand();
    cmd->new_area = m_drawing_area;
    m_backend->PushCommand(cmd);
    m_drawing_area_changed = false;
  }

  const GPURenderCommand rc{m_render_command.bits};
  switch (rc.primitive)
  {
    case GPUPrimitive::Polygon:
      DispatchPolygon(rc);
      break;

    case GPUPrimitive::Rectangle:
      DispatchRectangle(rc);
      break;

    case GPUPrimitive::Line:
    {
      if (!rc.polyline)
        DispatchLine(rc);
      else
        DispatchPolyLine(rc);
    }
    break;

    default:
      UnreachableCode();
      break;
  }
}

void GPU::DispatchPolygon(GPURenderCommand rc)
{
  const u32 num_vertices = rc.quad_polygon ? 4 : 3;
  const u32 first_color = rc.color_for_first_vertex;
  const bool shaded = rc.shading_enable;
  const bool textured = rc.texture_enable;
  std::array<GSVector2i, 4> positions;

  if (!m_backend->UsesPreciseVertices())
  {
    GPUBackendDrawPolygonCommand* cmd = m_backend->NewDrawPolygonCommand(num_vertices);
    FillDrawCommand(cmd, rc);
    for (u32 i = 0; i < num_vertices; i++)
    {
      GPUBackendDrawPolygonCommand::Vertex* vert = &cmd->vertices[i];
      vert->color = (shaded && i > 0) ? (FifoPop() & UINT32_C(0x00FFFFFF)) : first_color;
      const GPUVertexPosition vp{Truncate32(m_fifo.Pop())};
      vert->x = m_drawing_offset.x + vp.x;
      vert->y = m_drawing_offset.y + vp.y;
      vert->texcoord = textured ? Truncate16(FifoPop()) : 0;
      positions[i] = GSVector2i::load(&vert->x);
    }

    if (AddPolygonTicks(positions.data(), rc))
      m_backend->PushCommand(cmd);
    return;
  }

  GPUBackendDrawPrecisePolygonCommand* cmd = m_backend->NewDrawPrecisePolygonCommand(num_vertices);
  FillDrawCommand(cmd, rc);
  cmd->valid_w = g_settings.gpu_pgxp_texture_correction;
  for (u32 i = 0; i < num_vertices; i++)
  {
    GPUBackendDrawPrecisePolygonCommand::Vertex* vert = &cmd->vertices[i];
    vert->color = (shaded && i > 0) ? (FifoPop() & UINT32_C(0x00FFFFFF)) : first_color;
    const u64 maddr_and_pos = m_fifo.Pop();
    const GPUVertexPosition vp{Truncate32(maddr_and_pos)};
    vert->native_x = m_drawing_offset.x + vp.x;
    vert->native_y = m_drawing_offset.y + vp.y;
    vert->texcoord = textured ? Truncate16(FifoPop()) : 0;
    positions[i] = GSVector2i(vert->native_x, vert->native_y);
    GetPreciseVertexPosition(maddr_and_pos, vert->native_x, vert->native_y, &vert->x, &vert->y, &vert->w,
                             &cmd->valid_w);
  }

  if (AddPolygonTicks(positions.data(), rc))
    m_backend->PushCommand(cmd);
}

void GPU::DispatchRectangle(GPURenderCommand rc)
{
  GPUBackendDrawRectangleCommand* cmd = m_backend->NewDrawRectangleCommand();
  FillDrawCommand(cmd, rc);
  cmd->color = rc.color_for_first_vertex;

  const GPUVertexPosition vp{FifoPop()};
  cmd->x = TruncateGPUVertexPosition(m_drawing_offset.x + vp.x);
  cmd->y = TruncateGPUVertexPosition(m_drawing_offset.y + vp.y);

  if (rc.texture_enable)
  {
    const u32 texcoord_and_palette = FifoPop();
    cmd->palette.bits = Truncate16(texcoord_and_palette >> 16);
    cmd->texcoord = Truncate16(texcoord_and_palette);
  }
  else
  {
    cmd->palette.bits = 0;
    cmd->texcoord = 0;
  }

  switch (rc.rectangle_size)
  {
    case GPUDrawRectangleSize::R1x1:
      cmd->width = 1;
      cmd->height = 1;
      break;
    case GPUDrawRectangleSize::R8x8:
      cmd->width = 8;
      cmd->height = 8;
      break;
    case GPUDrawRectangleSize::R16x16:
      cmd->width = 16;
      cmd->height = 16;
      break;
    default:
    {
      const u32 width_and_height = FifoPop();
      cmd->width = static_cast<u16>(width_and_height & VRAM_WIDTH_MASK);
      cmd->height = static_cast<u16>((width_and_height >> 16) & VRAM_HEIGHT_MASK);
    }
    break;
  }

  const GSVector4i rect = GSVector4i(cmd->x, cmd->y, cmd->x + cmd->width, cmd->y + cmd->height);
  const GSVector4i clamped_rect = m_clamped_drawing_area.rintersect(rect);
  if (clamped_rect.rempty()) [[unlikely]]
  {
    DEBUG_LOG("Culling off-screen rectangle {}", rect);
    return;
  }

  AddDrawRectangleTicks(clamped_rect, rc.texture_enable, rc.transparency_enable);

  m_backend->PushCommand(cmd);
}

void GPU::DispatchLine(GPURenderCommand rc)
{
  const u32 start_color = rc.color_for_first_vertex;
  const u64 start_word = m_fifo.Pop();
  const u32 end_color = rc.shading_enable ? (FifoPop() & UINT32_C(0x00FFFFFF)) : start_color;
  const u64 end_word = m_fifo.Pop();
  const GPUVertexPosition start_vp{Truncate32(start_word)};
  const GPUVertexPosition end_vp{Truncate32(end_word)};
  const GSVector2i start_pos(m_drawing_offset.x + start_vp.x, m_drawing_offset.y + start_vp.y);
  const GSVector2i end_pos(m_drawing_offset.x + end_vp.x, m_drawing_offset.y + end_vp.y);

  bool visible;
  if (!AddLineSegmentTicks(start_pos, end_pos, rc, &visible) || !visible)
    return;

  if (!m_backend->UsesPreciseVertices())
  {
    GPUBackendDrawLineCommand* cmd = m_backend->NewDrawLineCommand(2);
    FillDrawCommand(cmd, rc);
    cmd->palette.bits = 0;
    cmd->vertices[0].Set(start_pos.x, start_pos.y, start_color);
    cmd->vertices[1].Set(end_pos.x, end_pos.y, end_color);
    m_backend->PushCommand(cmd);
    return;
  }

  GPUBackendDrawPreciseLineCommand* cmd = m_backend->NewDrawPreciseLineCommand(2);
  FillDrawCommand(cmd, rc);
  cmd->palette.bits = 0;
  SetPreciseLineVertex(&cmd->vertices[0], start_word, start_pos, start_color);
  SetPreciseLineVertex(&cmd->vertices[1], end_word, end_pos, end_color);
  m_backend->PushCommand(cmd);
}

void GPU::SetPreciseLineVertex(GPUBackendDrawPreciseLineCommand::Vertex* vert, u64 maddr_and_pos, GSVector2i pos,
                               u32 color) const
{
  vert->native_x = pos.x;
  vert->native_y = pos.y;
  vert->color = color;

  float w;
  bool valid_w = true;
  GetPreciseVertexPosition(maddr_and_pos, pos.x, pos.y, &vert->x, &vert->y, &w, &valid_w);
}

void GPU::DispatchPolyLine(GPURenderCommand rc)
{
  const u32 num_vertices = GetPolyLineVertexCount();
  const bool shaded = rc.shading_enable;
  const bool precise = m_backend->UsesPreciseVertices();

  GPUBackendDrawLineCommand* cmd = nullptr;
  GPUBackendDrawPreciseLineCommand* precise_cmd = nullptr;
  if (precise)
  {
    precise_cmd = m_backend->NewDrawPreciseLineCommand(num_vertices);
    FillDrawCommand(precise_cmd, rc);
  }
  else
  {
    cmd = m_backend->NewDrawLineCommand(num_vertices);
    FillDrawCommand(cmd, rc);
  }

  u32 buffer_pos = 0;
  GSVector2i last_pos;
  for (u32 i = 0; i < num_vertices; i++)
  {
    const u32 color = (shaded && i > 0) ? (Truncate32(m_polyline_buffer[buffer_pos++]) & UINT32_C(0x00FFFFFF)) :
                                          rc.color_for_first_vertex;
    const u64 maddr_and_pos = m_polyline_buffer[buffer_pos++];
    const GPUVertexPosition vp{Truncate32(maddr_and_pos)};
    const GSVector2i pos(m_drawing_offset.x + vp.x, m_drawing_offset.y + vp.y);
    if (precise)
      SetPreciseLineVertex(&precise_cmd->vertices[i], maddr_and_pos, pos, color);
    else
      cmd->vertices[i].Set(pos.x, pos.y, color);

    if (i > 0)
    {
      if (i == (num_vertices - 1))
        HoldDMARequestThroughCurrentTicks();

      bool visible;
      AddLineSegmentTicks(last_pos, pos, rc, &visible);
    }

    last_pos = pos;
  }

  m_backend->PushCommand(precise ? static_cast<GPUBackendCommand*>(precise_cmd) : static_cast<GPUBackendCommand*>(cmd));
}

void GPU::ReadVRAM(u32 x, u32 y, u32 width, u32 height)
{
  m_backend->Sync(false);
}

void GPU::FillVRAM(u32 x, u32 y, u32 width, u32 height, u32 color)
{
  GPUBackendFillVRAMCommand* cmd = m_backend->NewFillVRAMCommand();
  FillBackendCommandParameters(cmd);
  cmd->x = static_cast<u16>(x);
  cmd->y = static_cast<u16>(y);
  cmd->width = static_cast<u16>(width);
  cmd->height = static_cast<u16>(height);
  cmd->color = color;
  m_backend->PushCommand(cmd);
}

void GPU::UpdateVRAM(u32 x, u32 y, u32 width, u32 height, const void* data, bool set_mask, bool check_mask)
{
  const u32 num_words = width * height;
  GPUBackendUpdateVRAMCommand* cmd = m_backend->NewUpdateVRAMCommand(num_words);
  FillBackendCommandParameters(cmd);
  cmd->params.set_mask_while_drawing = set_mask;
  cmd->params.check_mask_before_draw = check_mask;
  cmd->x = static_cast<u16>(x);
  cmd->y = static_cast<u16>(y);
  cmd->width = static_cast<u16>(width);
  cmd->height = static_cast<u16>(height);
  std::memcpy(cmd->data, data, sizeof(u16) * num_words);
  m_backend->PushCommand(cmd);
}

void GPU::CopyVRAM(u32 src_x, u32 src_y, u32 dst_x, u32 dst_y, u32 width, u32 height)
{
  GPUBackendCopyVRAMCommand* cmd = m_backend->NewCopyVRAMCommand();
  FillBackendCommandParameters(cmd);
  cmd->src_x = static_cast<u16>(src_x);
  cmd->src_y = static_cast<u16>(src_y);
  cmd->dst_x = static_cast<u16>(dst_x);
  cmd->dst_y = static_cast<u16>(dst_y);
  cmd->width = static_cast<u16>(width);
  cmd->height = static_cast<u16>(height);
  m_backend->PushCommand(cmd);
}

void GPU::UpdateCLUT(GPUTexturePaletteReg reg, bool clut_is_8bit)
{
  GPUBackendUpdateCLUTCommand* cmd = m_backend->NewUpdateCLUTCommand();
  FillBackendCommandParameters(cmd);
  cmd->reg.bits = reg.bits;
  cmd->clut_is_8bit = clut_is_8bit;
  m_backend->PushCommand(cmd);
}
