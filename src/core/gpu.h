// SPDX-FileCopyrightText: 2019-2024 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only

#pragma once
#include "gpu_types.h"
#include "timers.h"
#include "timing_event.h"
#include "types.h"

#include "util/gpu_texture.h"

#include "common/bitfield.h"
#include "common/fifo_queue.h"
#include "common/types.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

class SmallStringBase;

class StateWrapper;

class GPUDevice;
class GPUBackend;
class GPUTexture;
class GPUPipeline;
class MediaCapture;

struct Settings;

namespace Threading {
class Thread;
}

class GPU
{
public:
  enum class BlitterState : u8
  {
    Idle,
    ReadingVRAM,
    WritingVRAM,
    DrawingPolyLine
  };

  enum class DMADirection : u32
  {
    Off = 0,
    FIFO = 1,
    CPUtoGP0 = 2,
    GPUREADtoCPU = 3
  };

  enum : u32
  {
    MAX_FIFO_SIZE = 4096,
    FIFO_SIZE = 16,
    FIFO_MODE_FULL_WORDS = 11,
    MAX_POLYLINE_VERTICES = 1024,
    DOT_TIMER_INDEX = 0,
    HBLANK_TIMER_INDEX = 1,
    MAX_RESOLUTION_SCALE = 32,
    DEINTERLACE_BUFFER_COUNT = 4,
    DRAWING_AREA_COORD_MASK = 1023,
  };

  enum : u16
  {
    NTSC_HALF_TICKS_PER_LINE = 6825,
    NTSC_TOTAL_LINES = 263,
    PAL_HALF_TICKS_PER_LINE = 6810,
    PAL_TOTAL_LINES = 314,
  };

  static constexpr u64 CRTC_SUBTICKS_PER_TICK = 451584;
  static constexpr u32 LINE_COUNTER_WRAP = 1024;

  enum : u16
  {
    NTSC_HORIZONTAL_ACTIVE_START = 488,
    NTSC_HORIZONTAL_ACTIVE_END = 3288,
    NTSC_VERTICAL_ACTIVE_START = 16,
    NTSC_VERTICAL_ACTIVE_END = 256,
    PAL_HORIZONTAL_ACTIVE_START = 487,
    PAL_HORIZONTAL_ACTIVE_END = 3282,
    PAL_VERTICAL_ACTIVE_START = 20,
    PAL_VERTICAL_ACTIVE_END = 308,
  };

  // Base class constructor.
  GPU();
  virtual ~GPU();

  virtual const Threading::Thread* GetSWThread() const = 0;
  virtual bool IsHardwareRenderer() const = 0;

  virtual bool Initialize();
  virtual void Reset(bool clear_vram);
  virtual bool DoState(StateWrapper& sw, GPUTexture** save_to_texture, bool update_display);

  // Graphics API state reset/restore - call when drawing the UI etc.
  // TODO: replace with "invalidate cached state"
  virtual void RestoreDeviceContext();

  // Render statistics debug window.
  void DrawDebugStateWindow();
  void GetStatsString(SmallStringBase& str);
  void GetMemoryStatsString(SmallStringBase& str);
  void ResetStatistics();
  void UpdateStatistics(u32 frame_count);

  void CPUClockChanged();

  // MMIO access
  u32 ReadRegister(u32 offset);
  void WriteRegister(u32 offset, u32 value);

  // DMA access
  void DMARead(u32* words, u32 word_count);

  ALWAYS_INLINE bool BeginDMAWrite() const
  {
    return (m_GPUSTAT.dma_direction == DMADirection::CPUtoGP0 || m_GPUSTAT.dma_direction == DMADirection::FIFO);
  }
  ALWAYS_INLINE void DMAWrite(u32 address, u32 value)
  {
    if (!m_fifo.IsFull())
      m_fifo.Push((ZeroExtend64(address) << 32) | ZeroExtend64(value));
  }
  void EndDMAWrite();

  /// Returns true if no data is being sent from VRAM to the DAC or that no portion of VRAM would be visible on screen.
  ALWAYS_INLINE bool IsDisplayDisabled() const
  {
    return m_GPUSTAT.display_disable || m_crtc_state.display_vram_width == 0 || m_crtc_state.display_vram_height == 0;
  }

  /// Returns true if scanout should be interlaced.
  ALWAYS_INLINE bool IsInterlacedDisplayEnabled() const
  {
    return (!m_force_progressive_scan && m_GPUSTAT.vertical_interlace);
  }

  /// Returns true if interlaced rendering is enabled and force progressive scan is disabled.
  ALWAYS_INLINE bool IsInterlacedRenderingEnabled() const
  {
    return (!m_force_progressive_scan && m_GPUSTAT.SkipDrawingToActiveField());
  }

  /// Returns true if we're in PAL mode, otherwise false if NTSC.
  ALWAYS_INLINE bool IsInPALMode() const { return m_GPUSTAT.pal_mode; }

  u64 GetPendingCRTCSubTicks() const;
  TickCount GetPendingCommandTicks() const;

  /// Returns true if enough ticks have passed for the raster to be on the next line.
  bool IsCRTCScanlinePending() const;

  /// Returns true if a raster scanline or command execution is pending.
  bool IsCommandCompletionPending() const;

  /// Synchronizes the CRTC, updating the hblank timer.
  void SynchronizeCRTC();

  /// Reschedules the CRTC event after a timer it clocks was changed. Synchronize the CRTC before the change.
  void RescheduleCRTC();

  /// Recompile shaders/recreate framebuffers when needed.
  virtual void UpdateSettings(const Settings& old_settings);

  /// Updates the resolution scale when it's set to automatic.
  virtual void UpdateResolutionScale();

  /// Returns the effective display resolution of the GPU.
  virtual std::tuple<u32, u32> GetEffectiveDisplayResolution(bool scaled = true);

  /// Returns the full display resolution of the GPU, including padding.
  virtual std::tuple<u32, u32> GetFullDisplayResolution(bool scaled = true);

  float ComputeHorizontalFrequency() const;
  float ComputeVerticalFrequency() const;
  float ComputeDisplayAspectRatio() const;

  static std::unique_ptr<GPU> CreateHardwareRenderer();
  static std::unique_ptr<GPU> CreateSoftwareRenderer();

  // Converts window coordinates into horizontal ticks and scanlines. Returns false if out of range. Used for lightguns.
  void ConvertScreenCoordinatesToDisplayCoordinates(float window_x, float window_y, float* display_x,
                                                    float* display_y) const;
  bool ConvertDisplayCoordinatesToBeamTicksAndLines(float display_x, float display_y, float x_scale, u32* out_tick,
                                                    u32* out_line) const;

  // Returns the current beam position.
  void GetBeamPosition(u32* out_ticks, u32* out_line);

  // Returns the number of system clock ticks until the specified tick/line.
  TickCount GetSystemTicksUntilTicksAndLine(u32 ticks, u32 line);

  // Returns the number of visible lines.
  ALWAYS_INLINE u16 GetCRTCActiveStartLine() const { return m_crtc_state.vertical_display_start; }
  ALWAYS_INLINE u16 GetCRTCActiveEndLine() const { return m_crtc_state.vertical_display_end; }

  // Returns the video clock frequency.
  TickCount GetCRTCFrequency() const;
  ALWAYS_INLINE u16 GetCRTCDotClockDivider() const { return m_crtc_state.dot_clock_divider; }
  ALWAYS_INLINE s32 GetCRTCDisplayWidth() const { return m_crtc_state.display_width; }
  ALWAYS_INLINE s32 GetCRTCDisplayHeight() const { return m_crtc_state.display_height; }

  // Dumps raw VRAM to a file.
  bool DumpVRAMToFile(const char* filename);

  // Ensures all buffered vertices are drawn.
  virtual void FlushRender() = 0;

  /// Helper function for computing the draw rectangle in a larger window.
  void CalculateDrawRect(s32 window_width, s32 window_height, bool apply_rotation, bool apply_aspect_ratio,
                         GSVector4i* display_rect, GSVector4i* draw_rect) const;

  /// Helper function for computing screenshot bounds.
  void CalculateScreenshotSize(DisplayScreenshotMode mode, u32* width, u32* height, GSVector4i* display_rect,
                               GSVector4i* draw_rect) const;

  /// Helper function to save current display texture to PNG.
  bool WriteDisplayTextureToFile(std::string filename, bool compress_on_thread = false);

  bool ReadDisplayTexture(std::vector<u32>* pixels, u32* stride, u32* width, u32* height, GPUTexture::Format* format);

  ALWAYS_INLINE u32 GetDisplayTextureViewWidth() const { return static_cast<u32>(m_display_texture_view_width); }
  ALWAYS_INLINE u32 GetDisplayTextureViewHeight() const { return static_cast<u32>(m_display_texture_view_height); }
  bool RenderDisplayAtInternalResolution(GPUTexture* target, bool postfx);

  /// Renders the display, optionally with postprocessing to the specified image.
  bool RenderScreenshotToBuffer(u32 width, u32 height, const GSVector4i display_rect, const GSVector4i draw_rect,
                                bool postfx, std::vector<u32>* out_pixels, u32* out_stride,
                                GPUTexture::Format* out_format);

  /// Helper function to save screenshot to PNG.
  bool RenderScreenshotToFile(std::string filename, DisplayScreenshotMode mode, u8 quality, bool compress_on_thread,
                              bool show_osd_message);

  /// Draws the current display texture, with any post-processing.
  bool PresentDisplay();

  /// Sends the current frame to media capture.
  bool SendDisplayToMediaCapture(MediaCapture* cap);

  /// Reads the CLUT from the specified coordinates, accounting for wrap-around.
  static void ReadCLUT(u16* dest, GPUTexturePaletteReg reg, bool clut_is_8bit);

  bool IsDeviceOnBackendThread() const;

protected:
  u64 GetCRTCSubTicksPerSystemTick() const;
  TickCount CRTCSubTicksToSystemTicks(u64 subticks) const;
  TickCount SystemTicksToCRTCTicks(TickCount sysclk_ticks, TickCount* fractional_ticks) const;
  u64 GetCRTCLineSubTicks() const;
  u64 GetCRTCPositionInLineSubTicks() const;
  u64 GetDotsPerLine() const;
  u32 GetCRTCWrapLine() const;
  u64 GetCRTCBeamSubTicks() const;

  static constexpr std::tuple<u8, u8> UnpackTexcoord(u16 texcoord)
  {
    return std::make_tuple(static_cast<u8>(texcoord), static_cast<u8>(texcoord >> 8));
  }

  static constexpr std::tuple<u8, u8, u8> UnpackColorRGB24(u32 rgb24)
  {
    return std::make_tuple(static_cast<u8>(rgb24), static_cast<u8>(rgb24 >> 8), static_cast<u8>(rgb24 >> 16));
  }

  static bool DumpVRAMToFile(const char* filename, u32 width, u32 height, u32 stride, const void* buffer,
                             bool remove_alpha);

  void SoftReset();
  void ClearDisplay();

  // Sets dots per scanline
  void UpdateCRTCConfig();
  void UpdateCRTCDisplayParameters();

  // Update ticks for this execution slice
  void UpdateCRTCTickEvent();
  void UpdateCommandTickEvent();

  // Updates dynamic bits in GPUSTAT (ready to send VRAM/ready to receive DMA)
  void UpdateDMARequest();
  void UpdateGPUIdle();

  // Ticks for hblank/vblank.
  void CRTCTickEvent(TickCount ticks);
  void SetCRTCVBlank(bool vblank);
  void ResetCRTCTiming(bool was_in_hblank);
  void CommandTickEvent(TickCount ticks);

  /// Returns 0 if the currently-displayed field is on odd lines (1,3,5,...) or 1 if even (2,4,6,...).
  ALWAYS_INLINE u32 GetInterlacedDisplayField() const { return ZeroExtend32(m_crtc_state.interlaced_field); }

  /// Returns 0 if the currently-displayed field is on an even line in VRAM, otherwise 1.
  ALWAYS_INLINE u32 GetActiveLineLSB() const { return ZeroExtend32(m_crtc_state.active_line_lsb); }

  /// Updates drawing area that's suitablef or clamping.
  void SetClampedDrawingArea();

  /// Sets/decodes GP0(E1h) (set draw mode).
  void SetDrawMode(u16 bits);

  /// Sets/decodes polygon/rectangle texture palette value.
  void SetTexturePalette(u16 bits);

  /// Sets/decodes texture window bits.
  void SetTextureWindow(u32 value);

  u32 ReadGPUREAD();
  void FinishVRAMWrite(bool write_partial_row = true);
  void DrawBufferedPolyLineSegment();
  bool IsLoadedStateValid() const;

  /// Returns the number of vertices in the buffered poly-line.
  ALWAYS_INLINE u32 GetPolyLineVertexCount() const
  {
    return (static_cast<u32>(m_polyline_buffer.size()) + BoolToUInt32(m_render_command.shading_enable)) >>
           BoolToUInt8(m_render_command.shading_enable);
  }

  void AddCommandTicks(TickCount ticks);
  void HoldDMARequestThroughCurrentTicks();

  void WriteGP1(u32 value);
  void EndCommand();
  void ExecuteCommands();
  void TryExecuteCommands();
  void HandleGetGPUInfoCommand(u32 value);
  void UpdateCLUTIfNeeded(GPUTextureMode texmode, GPUTexturePaletteReg clut);
  void InvalidateCLUT();
  bool IsCLUTValid() const;

  // Rendering in the backend
  virtual void ReadVRAM(u32 x, u32 y, u32 width, u32 height);
  void FillVRAM(u32 x, u32 y, u32 width, u32 height, u32 color);
  void UpdateVRAM(u32 x, u32 y, u32 width, u32 height, const void* data, bool set_mask, bool check_mask);
  void CopyVRAM(u32 src_x, u32 src_y, u32 dst_x, u32 dst_y, u32 width, u32 height);
  void DispatchRenderCommand();
  void UpdateCLUT(GPUTexturePaletteReg reg, bool clut_is_8bit);

  void FillBackendCommandParameters(GPUBackendCommand* cmd) const;
  void FillDrawCommand(GPUBackendDrawCommand* cmd, GPURenderCommand rc);
  void DispatchPolygon(GPURenderCommand rc);
  void DispatchRectangle(GPURenderCommand rc);
  void DispatchLine(GPURenderCommand rc);
  void DispatchPolyLine(GPURenderCommand rc);
  bool AddPolygonTicks(const GSVector2i* positions, GPURenderCommand rc);
  bool AddLineSegmentTicks(GSVector2i start, GSVector2i end, GPURenderCommand rc, bool* out_visible);
  void GetPreciseVertexPosition(u64 maddr_and_pos, s32 native_x, s32 native_y, float* x, float* y, float* w,
                                bool* valid_w) const;
  void SetPreciseLineVertex(GPUBackendDrawPreciseLineCommand::Vertex* vert, u64 maddr_and_pos, GSVector2i pos,
                            u32 color) const;
  virtual void UpdateDisplay() = 0;
  virtual void DrawRendererStats();
  virtual void OnBufferSwapped();

  ALWAYS_INLINE_RELEASE void AddDrawTriangleTicks(GSVector2i v1, GSVector2i v2, GSVector2i v3, bool shaded,
                                                  bool textured, bool semitransparent)
  {
    // This will not produce the correct results for triangles which are partially outside the clip area.
    // However, usually it'll undershoot not overshoot. If we wanted to make this more accurate, we'd need to intersect
    // the edges with the clip rectangle.
    // TODO: Coordinates are exclusive, so off by one here...
    const GSVector2i clamp_min = GSVector2i::load(&m_clamped_drawing_area.x);
    const GSVector2i clamp_max = GSVector2i::load(&m_clamped_drawing_area.z);
    v1 = v1.sat_i32(clamp_min, clamp_max);
    v2 = v2.sat_i32(clamp_min, clamp_max);
    v3 = v3.sat_i32(clamp_min, clamp_max);

    TickCount pixels =
      std::abs((v1.x * v2.y + v2.x * v3.y + v3.x * v1.y - v1.x * v3.y - v2.x * v1.y - v3.x * v2.y) / 2);
    TickCount rows = std::max({v1.y, v2.y, v3.y}) - std::min({v1.y, v2.y, v3.y});
    if (m_GPUSTAT.SkipDrawingToActiveField())
    {
      pixels /= 2;
      rows /= 2;
    }

    const bool semi = (semitransparent || m_GPUSTAT.check_mask_before_draw);
    const bool flat = !shaded && !textured;
    const TickCount pixel_quarter_ticks = flat ? (semi ? 3 : 2) : 4;
    const TickCount row_ticks = 2 + (semi ? (flat ? 3 : 1) : 0);
    AddCommandTicks((pixels * pixel_quarter_ticks + 3) / 4 + rows * row_ticks);
  }
  ALWAYS_INLINE_RELEASE void AddDrawRectangleTicks(const GSVector4i clamped_rect, bool textured, bool semitransparent)
  {
    u32 drawn_width = clamped_rect.width();
    u32 drawn_height = clamped_rect.height();

    const bool semi = (semitransparent || m_GPUSTAT.check_mask_before_draw);
    const u32 pixels_per_row = semi ? drawn_width : (drawn_width & ~1u);
    u32 eighth_ticks_per_row = (semi ? 64 : 16) + pixels_per_row * (semi ? 6 : 4);
    if (textured)
    {
      switch (m_draw_mode.mode_reg.texture_mode)
      {
        case GPUTextureMode::Palette4Bit:
          eighth_ticks_per_row += drawn_width * 6;
          break;

        case GPUTextureMode::Palette8Bit:
        {
          if (drawn_width > 128)
            eighth_ticks_per_row += drawn_width * 11;
          else if ((drawn_width * drawn_height) > 2048)
            eighth_ticks_per_row += ((drawn_width / 4) * (4 * (128 / drawn_width))) * 11 / 2;
          else
            eighth_ticks_per_row += drawn_width * 11 / 2;
        }
        break;

        case GPUTextureMode::Direct16Bit:
        case GPUTextureMode::Reserved_Direct16Bit:
        {
          if (drawn_width > 128)
            eighth_ticks_per_row += drawn_width * 20;
          else if ((drawn_width * drawn_height) > 1024)
            eighth_ticks_per_row += ((drawn_width / 4) * (8 * (128 / drawn_width))) * 5;
          else
            eighth_ticks_per_row += drawn_width * 5;
        }
        break;

          DefaultCaseIsUnreachable()
      }
    }

    if (drawn_width > 0 && drawn_width < 8)
      eighth_ticks_per_row = std::max(eighth_ticks_per_row, 36u);
    if (m_GPUSTAT.SkipDrawingToActiveField())
      drawn_height = std::max<u32>(drawn_height / 2, 1u);

    AddCommandTicks((eighth_ticks_per_row * drawn_height + 7) / 8);
  }
  ALWAYS_INLINE_RELEASE void AddDrawLineTicks(const GSVector4i clamped_rect, bool shaded, bool semitransparent)
  {
    AddCommandTicks(shaded ? 124 : 60);
    if (clamped_rect.rempty())
      return;

    u32 drawn_height = clamped_rect.height();
    u32 major = std::max<u32>(clamped_rect.width(), drawn_height);
    if (m_GPUSTAT.SkipDrawingToActiveField())
    {
      const u32 field_height = std::max<u32>(drawn_height / 2, 1u);
      major = major * field_height / drawn_height;
      drawn_height = field_height;
    }

    const bool semi = (semitransparent || m_GPUSTAT.check_mask_before_draw);
    AddCommandTicks((major * 4 + drawn_height * (semi ? 26 : 16) + 3) / 4);
  }

  TimingEvent m_crtc_tick_event;
  TimingEvent m_command_tick_event;

  union GPUSTAT
  {
    u32 bits;
    BitField<u32, u8, 0, 4> texture_page_x_base;
    BitField<u32, u8, 4, 1> texture_page_y_base;
    BitField<u32, GPUTransparencyMode, 5, 2> semi_transparency_mode;
    BitField<u32, GPUTextureMode, 7, 2> texture_color_mode;
    BitField<u32, bool, 9, 1> dither_enable;
    BitField<u32, bool, 10, 1> draw_to_displayed_field;
    BitField<u32, bool, 11, 1> set_mask_while_drawing;
    BitField<u32, bool, 12, 1> check_mask_before_draw;
    BitField<u32, u8, 13, 1> interlaced_field;
    BitField<u32, bool, 14, 1> reverse_flag;
    BitField<u32, bool, 15, 1> texture_page_y_base_2;
    BitField<u32, u8, 16, 1> horizontal_resolution_2;
    BitField<u32, u8, 17, 2> horizontal_resolution_1;
    BitField<u32, bool, 19, 1> vertical_resolution;
    BitField<u32, bool, 20, 1> pal_mode;
    BitField<u32, bool, 21, 1> display_area_color_depth_24;
    BitField<u32, bool, 22, 1> vertical_interlace;
    BitField<u32, bool, 23, 1> display_disable;
    BitField<u32, bool, 24, 1> interrupt_request;
    BitField<u32, bool, 25, 1> dma_data_request;
    BitField<u32, bool, 26, 1> gpu_idle;
    BitField<u32, bool, 27, 1> ready_to_send_vram;
    BitField<u32, bool, 28, 1> ready_to_recieve_dma;
    BitField<u32, DMADirection, 29, 2> dma_direction;
    BitField<u32, bool, 31, 1> display_line_lsb;

    ALWAYS_INLINE bool IsMaskingEnabled() const
    {
      static constexpr u32 MASK = ((1 << 11) | (1 << 12));
      return ((bits & MASK) != 0);
    }
    ALWAYS_INLINE bool SkipDrawingToActiveField() const
    {
      static constexpr u32 MASK = (1 << 19) | (1 << 22) | (1 << 10);
      static constexpr u32 ACTIVE = (1 << 19) | (1 << 22);
      return ((bits & MASK) == ACTIVE);
    }
    ALWAYS_INLINE bool InInterleaved480iMode() const
    {
      static constexpr u32 ACTIVE = (1 << 19) | (1 << 22);
      return ((bits & ACTIVE) == ACTIVE);
    }

    // During transfer/render operations, if ((dst_pixel & mask_and) == 0) { pixel = src_pixel | mask_or }
    ALWAYS_INLINE u16 GetMaskAND() const
    {
      // return check_mask_before_draw ? 0x8000 : 0x0000;
      return Truncate16((bits << 3) & 0x8000);
    }
    ALWAYS_INLINE u16 GetMaskOR() const
    {
      // return set_mask_while_drawing ? 0x8000 : 0x0000;
      return Truncate16((bits << 4) & 0x8000);
    }
  } m_GPUSTAT = {};

  struct DrawMode
  {
    static constexpr u16 PALETTE_MASK = UINT16_C(0b0111111111111111);
    static constexpr u32 TEXTURE_WINDOW_MASK = UINT32_C(0b11111111111111111111);

    // original values
    GPUDrawModeReg mode_reg;
    GPUTexturePaletteReg palette_reg; // from vertex
    u32 texture_window_value;

    // decoded values
    GPUTextureWindow texture_window;
    bool texture_page_changed;
    bool texture_window_changed;

    ALWAYS_INLINE bool IsTexturePageChanged() const { return texture_page_changed; }
    ALWAYS_INLINE void SetTexturePageChanged() { texture_page_changed = true; }
    ALWAYS_INLINE void ClearTexturePageChangedFlag() { texture_page_changed = false; }

    ALWAYS_INLINE bool IsTextureWindowChanged() const { return texture_window_changed; }
    ALWAYS_INLINE void SetTextureWindowChanged() { texture_window_changed = true; }
    ALWAYS_INLINE void ClearTextureWindowChangedFlag() { texture_window_changed = false; }
  } m_draw_mode = {};

  std::unique_ptr<GPUBackend> m_backend;

  GPUDrawingArea m_drawing_area = {};
  GPUDrawingOffset m_drawing_offset = {};
  GSVector4i m_clamped_drawing_area = {};

  bool m_console_is_pal = false;
  bool m_enable_upper_vram = false;
  bool m_drawing_area_changed = false;
  bool m_force_progressive_scan = false;
  bool m_force_ntsc_timings = false;

  struct CRTCState
  {
    struct Regs
    {
      static constexpr u32 DISPLAY_ADDRESS_START_MASK = 0b111'11111111'11111110;
      static constexpr u32 HORIZONTAL_DISPLAY_RANGE_MASK = 0b11111111'11111111'11111111;
      static constexpr u32 VERTICAL_DISPLAY_RANGE_MASK = 0b1111'11111111'11111111;

      union
      {
        u32 display_address_start;
        BitField<u32, u16, 0, 10> X;
        BitField<u32, u16, 10, 9> Y;
      };
      union
      {
        u32 horizontal_display_range;
        BitField<u32, u16, 0, 12> X1;
        BitField<u32, u16, 12, 12> X2;
      };

      union
      {
        u32 vertical_display_range;
        BitField<u32, u16, 0, 10> Y1;
        BitField<u32, u16, 10, 10> Y2;
      };
    } regs;

    u16 dot_clock_divider;

    // Size of the simulated screen in pixels. Depending on crop mode, this may include overscan area.
    u16 display_width;
    u16 display_height;

    // Top-left corner in screen coordinates where the outputted portion of VRAM is first visible.
    u16 display_origin_left;
    u16 display_origin_top;

    // Rectangle in VRAM coordinates describing the area of VRAM that is visible on screen.
    u16 display_vram_left;
    u16 display_vram_top;
    u16 display_vram_width;
    u16 display_vram_height;

    // Visible range of the screen, in GPU ticks/lines. Clamped to lie within the active video region.
    u16 horizontal_visible_start;
    u16 horizontal_visible_end;
    u16 vertical_visible_start;
    u16 vertical_visible_end;

    u16 horizontal_display_start;
    u16 horizontal_display_end;
    u16 vertical_display_start;
    u16 vertical_display_end;

    u16 horizontal_active_start;
    u16 horizontal_active_end;

    u16 horizontal_total;
    u16 vertical_total;
    TickCount horizontal_total_fraction;

    TickCount fractional_ticks;
    TickCount current_tick_in_scanline;
    u32 current_scanline;


    bool in_hblank;
    bool in_vblank;

    u8 interlaced_field; // 0 = odd, 1 = even
    u8 interlaced_display_field;
    bool frame_end_missed;
    u8 active_line_lsb;

    ALWAYS_INLINE void UpdateHBlankFlag()
    {
      in_hblank =
        (current_tick_in_scanline < horizontal_active_start || current_tick_in_scanline >= horizontal_active_end);
    }
  } m_crtc_state = {};

public:
  struct DisplayState
  {
    CRTCState crtc_state;
    GPUSTAT gpustat;
    bool force_progressive_scan;

    DisplayState& operator=(const DisplayState& rhs)
    {
      std::memcpy(static_cast<void*>(this), &rhs, sizeof(DisplayState));
      return *this;
    }

    ALWAYS_INLINE bool IsDisplayDisabled() const
    {
      return gpustat.display_disable || crtc_state.display_vram_width == 0 || crtc_state.display_vram_height == 0;
    }
    ALWAYS_INLINE bool IsInterlacedDisplayEnabled() const
    {
      return (!force_progressive_scan && gpustat.vertical_interlace);
    }
  };

  DisplayState GetDisplayState() const;
  static float ComputeDisplayAspectRatio(const DisplayState& state);
  static void CalculateDrawRect(const DisplayState& state, s32 window_width, s32 window_height, bool apply_rotation,
                                bool apply_aspect_ratio, GSVector4i* display_rect, GSVector4i* draw_rect);

protected:
  DisplayState m_presented_display_state = {};

  BlitterState m_blitter_state = BlitterState::Idle;
  u32 m_command_total_words = 0;
  TickCount m_pending_command_ticks = 0;
  TickCount m_dreq_hold_ticks = 0;
  TickCount m_dreq_hold_start_ticks = 0;

  /// GPUREAD value for non-VRAM-reads.
  u32 m_GPUREAD_latch = 0;

  // These are the bits from the palette register, but zero extended to 32-bit, so we can have an "invalid" value.
  // If an extra byte is ever not needed here for padding, the 8-bit flag could be packed into the MSB of this value.
  u32 m_current_clut_reg_bits = {};
  bool m_current_clut_is_8bit = false;

  /// True if currently executing/syncing.
  bool m_executing_commands = false;

  struct VRAMTransfer
  {
    u16 x;
    u16 y;
    u16 width;
    u16 height;
    u16 col;
    u16 row;
  } m_vram_transfer = {};

  HeapFIFOQueue<u64, MAX_FIFO_SIZE> m_fifo;
  std::vector<u32> m_blit_buffer;
  std::vector<u64> m_polyline_buffer;
  u32 m_blit_remaining_words;
  GPURenderCommand m_render_command{};

  ALWAYS_INLINE u32 FifoPop() { return Truncate32(m_fifo.Pop()); }
  ALWAYS_INLINE u32 FifoPeek() { return Truncate32(m_fifo.Peek()); }
  ALWAYS_INLINE u32 FifoPeek(u32 i) { return Truncate32(m_fifo.Peek(i)); }

  TickCount m_max_run_ahead = 128;

  void ClearDisplayTexture();
  void SetDisplayTexture(GPUTexture* texture, GPUTexture* depth_texture, s32 view_x, s32 view_y, s32 view_width,
                         s32 view_height);

  bool RenderDisplay(GPUTexture* target, const GSVector4i display_rect, const GSVector4i draw_rect, bool postfx);

  bool Deinterlace(u32 field, u32 line_skip);
  bool DeinterlaceExtractField(u32 dst_bufidx, GPUTexture* src, u32 x, u32 y, u32 width, u32 height, u32 line_skip);
  bool DeinterlaceSetTargetSize(u32 width, u32 height, bool preserve);
  void DestroyDeinterlaceTextures();
  bool ApplyChromaSmoothing();

  u32 m_current_deinterlace_buffer = 0;
  std::unique_ptr<GPUPipeline> m_deinterlace_pipeline;
  std::unique_ptr<GPUPipeline> m_deinterlace_extract_pipeline;
  std::array<std::unique_ptr<GPUTexture>, DEINTERLACE_BUFFER_COUNT> m_deinterlace_buffers;
  std::unique_ptr<GPUTexture> m_deinterlace_texture;

  std::unique_ptr<GPUPipeline> m_chroma_smoothing_pipeline;
  std::unique_ptr<GPUTexture> m_chroma_smoothing_texture;

  std::unique_ptr<GPUPipeline> m_display_pipeline;
  GPUTexture* m_display_texture = nullptr;
  GPUTexture* m_display_depth_buffer = nullptr;
  s32 m_display_texture_view_x = 0;
  s32 m_display_texture_view_y = 0;
  s32 m_display_texture_view_width = 0;
  s32 m_display_texture_view_height = 0;

  struct Counters
  {
    u32 num_reads;
    u32 num_writes;
    u32 num_copies;
    u32 num_vertices;
    u32 num_primitives;

    // u32 num_read_texture_updates;
    // u32 num_ubo_updates;
  };

  struct Stats : Counters
  {
    size_t host_buffer_streamed;
    u32 host_num_draws;
    u32 host_num_barriers;
    u32 host_num_render_passes;
    u32 host_num_copies;
    u32 host_num_downloads;
    u32 host_num_uploads;
  };

  Counters m_counters = {};
  Stats m_stats = {};

private:
  bool CompileDisplayPipelines(bool display, bool deinterlace, bool chroma_smoothing);

  using GP0CommandHandler = bool (GPU::*)();
  using GP0CommandHandlerTable = std::array<GP0CommandHandler, 256>;
  static GP0CommandHandlerTable GenerateGP0CommandHandlerTable();
  static u32 GetGP0CommandWordCount(u32 command);
  static u32 GetGP0CommandHeldWords(u32 command);

  // Rendering commands, returns false if not enough data is provided
  bool HandleUnknownGP0Command();
  bool HandleNOPCommand();
  bool HandleClearCacheCommand();
  bool HandleInterruptRequestCommand();
  bool HandleSetDrawModeCommand();
  bool HandleSetTextureWindowCommand();
  bool HandleSetDrawingAreaTopLeftCommand();
  bool HandleSetDrawingAreaBottomRightCommand();
  bool HandleSetDrawingOffsetCommand();
  bool HandleSetMaskBitCommand();
  bool HandleRenderPolygonCommand();
  bool HandleRenderRectangleCommand();
  bool HandleRenderLineCommand();
  bool HandleRenderPolyLineCommand();
  bool HandleFillRectangleCommand();
  bool HandleCopyRectangleCPUToVRAMCommand();
  bool HandleCopyRectangleVRAMToCPUCommand();
  bool HandleCopyRectangleVRAMToVRAMCommand();

  static const GP0CommandHandlerTable s_GP0_command_handler_table;
};

extern std::unique_ptr<GPU> g_gpu;
extern u16 (&g_vram)[VRAM_SIZE / sizeof(u16)];
extern u16 g_gpu_clut[GPU_CLUT_SIZE];
