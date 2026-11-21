// SPDX-FileCopyrightText: 2019-2024 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only

#include "gpu_sw.h"
#include "gpu_sw_psxgpu_backend.h"
#include "system.h"

#include "util/gpu_device.h"

#include "common/align.h"
#include "common/assert.h"
#include "common/gsvector.h"
#include "common/gsvector_formatter.h"
#include "common/log.h"

#include <algorithm>

Log_SetChannel(GPU_SW);

GPU_SW::GPU_SW() = default;

GPU_SW::~GPU_SW()
{
  g_gpu_device->RecycleTexture(std::move(m_upload_texture));
  m_backend->Shutdown();
}

const Threading::Thread* GPU_SW::GetSWThread() const
{
  return m_backend->GetThread();
}

bool GPU_SW::IsHardwareRenderer() const
{
  return false;
}

bool GPU_SW::Initialize()
{
  if (!GPU::Initialize() || !CreateBackend())
    return false;

  static constexpr const std::array formats_for_16bit = {GPUTexture::Format::RGB565, GPUTexture::Format::RGBA5551,
                                                         GPUTexture::Format::RGBA8, GPUTexture::Format::BGRA8};
  static constexpr const std::array formats_for_24bit = {GPUTexture::Format::RGBA8, GPUTexture::Format::BGRA8,
                                                         GPUTexture::Format::RGB565, GPUTexture::Format::RGBA5551};
  for (const GPUTexture::Format format : formats_for_16bit)
  {
    if (g_gpu_device->SupportsTextureFormat(format))
    {
      m_16bit_display_format = format;
      break;
    }
  }
  for (const GPUTexture::Format format : formats_for_24bit)
  {
    if (g_gpu_device->SupportsTextureFormat(format))
    {
      m_24bit_display_format = format;
      break;
    }
  }

  return true;
}

bool GPU_SW::DoState(StateWrapper& sw, GPUTexture** host_texture, bool update_display)
{
  // need to ensure the worker thread is done
  m_backend->Sync(true);

  // ignore the host texture for software mode, since we want to save vram here
  return GPU::DoState(sw, nullptr, update_display);
}

void GPU_SW::Reset(bool clear_vram)
{
  GPU::Reset(clear_vram);

  m_backend->Reset();
}

bool GPU_SW::CreateBackend()
{
  if (m_backend)
  {
    m_backend->Sync(false);
    m_backend->Shutdown();
  }

  m_backend_is_fast_rasterizer = g_settings.gpu_software_fast_rasterizer;
  if (m_backend_is_fast_rasterizer)
    m_backend = std::make_unique<GPU_PSXGPU_Backend>();
  else
    m_backend = std::make_unique<GPU_SW_Backend>();

  return m_backend->Initialize(false);
}

void GPU_SW::UpdateSettings(const Settings& old_settings)
{
  GPU::UpdateSettings(old_settings);
  if (g_settings.gpu_software_fast_rasterizer != m_backend_is_fast_rasterizer)
  {
    FlushRender();
    CreateBackend();
    m_drawing_area_changed = true;
    InvalidateCLUT();
  }
  m_backend->UpdateSettings();
}

GPUTexture* GPU_SW::GetDisplayTexture(u32 width, u32 height, GPUTexture::Format format)
{
  if (!m_upload_texture || m_upload_texture->GetWidth() != width || m_upload_texture->GetHeight() != height ||
      m_upload_texture->GetFormat() != format)
  {
    ClearDisplayTexture();
    g_gpu_device->RecycleTexture(std::move(m_upload_texture));
    m_upload_texture =
      g_gpu_device->FetchTexture(width, height, 1, 1, 1, GPUTexture::Type::DynamicTexture, format, nullptr, 0);
    if (!m_upload_texture) [[unlikely]]
      ERROR_LOG("Failed to create {}x{} {} texture", width, height, static_cast<u32>(format));
  }

  return m_upload_texture.get();
}

template<GPUTexture::Format out_format, typename out_type>
static void CopyOutRow16(const u16* src_ptr, out_type* dst_ptr, u32 width);

template<GPUTexture::Format out_format, typename out_type>
static out_type VRAM16ToOutput(u16 value);

template<>
ALWAYS_INLINE u16 VRAM16ToOutput<GPUTexture::Format::RGBA5551, u16>(u16 value)
{
  return (value & 0x3E0) | ((value >> 10) & 0x1F) | ((value & 0x1F) << 10);
}

template<>
ALWAYS_INLINE u16 VRAM16ToOutput<GPUTexture::Format::RGB565, u16>(u16 value)
{
  return ((value & 0x3E0) << 1) | ((value & 0x20) << 1) | ((value >> 10) & 0x1F) | ((value & 0x1F) << 11);
}

template<>
ALWAYS_INLINE u32 VRAM16ToOutput<GPUTexture::Format::RGBA8, u32>(u16 value)
{
  const u32 value32 = ZeroExtend32(value);
  const u32 r = (value32 & 31u) << 3;
  const u32 g = ((value32 >> 5) & 31u) << 3;
  const u32 b = ((value32 >> 10) & 31u) << 3;
  const u32 a = ((value >> 15) != 0) ? 255 : 0;
  return ZeroExtend32(r) | (ZeroExtend32(g) << 8) | (ZeroExtend32(b) << 16) | (ZeroExtend32(a) << 24);
}

template<>
ALWAYS_INLINE u32 VRAM16ToOutput<GPUTexture::Format::BGRA8, u32>(u16 value)
{
  const u32 value32 = ZeroExtend32(value);
  const u32 r = (value32 & 31u) << 3;
  const u32 g = ((value32 >> 5) & 31u) << 3;
  const u32 b = ((value32 >> 10) & 31u) << 3;
  return ZeroExtend32(b) | (ZeroExtend32(g) << 8) | (ZeroExtend32(r) << 16) | (0xFF000000u);
}

template<>
ALWAYS_INLINE void CopyOutRow16<GPUTexture::Format::RGBA5551, u16>(const u16* src_ptr, u16* dst_ptr, u32 width)
{
  u32 col = 0;

  const u32 aligned_width = Common::AlignDownPow2(width, 8);
  for (; col < aligned_width; col += 8)
  {
    constexpr GSVector4i single_mask = GSVector4i::cxpr16(0x1F);
    GSVector4i value = GSVector4i::load<false>(src_ptr);
    src_ptr += 8;
    GSVector4i a = value & GSVector4i::cxpr16(0x3E0);
    GSVector4i b = value.srl16<10>() & single_mask;
    GSVector4i c = (value & single_mask).sll16<10>();
    value = (a | b) | c;
    GSVector4i::store<false>(dst_ptr, value);
    dst_ptr += 8;
  }

  for (; col < width; col++)
    *(dst_ptr++) = VRAM16ToOutput<GPUTexture::Format::RGBA5551, u16>(*(src_ptr++));
}

template<>
ALWAYS_INLINE void CopyOutRow16<GPUTexture::Format::RGB565, u16>(const u16* src_ptr, u16* dst_ptr, u32 width)
{
  u32 col = 0;

  const u32 aligned_width = Common::AlignDownPow2(width, 8);
  for (; col < aligned_width; col += 8)
  {
    constexpr GSVector4i single_mask = GSVector4i::cxpr16(0x1F);
    GSVector4i value = GSVector4i::load<false>(src_ptr);
    src_ptr += 8;
    GSVector4i a = (value & GSVector4i::cxpr16(0x3E0)).sll16<1>(); // (value & 0x3E0) << 1
    GSVector4i b = (value & GSVector4i::cxpr16(0x20)).sll16<1>();  // (value & 0x20) << 1
    GSVector4i c = (value.srl16<10>() & single_mask);              // ((value >> 10) & 0x1F)
    GSVector4i d = (value & single_mask).sll16<11>();              // ((value & 0x1F) << 11)
    value = (((a | b) | c) | d);
    GSVector4i::store<false>(dst_ptr, value);
    dst_ptr += 8;
  }

  for (; col < width; col++)
    *(dst_ptr++) = VRAM16ToOutput<GPUTexture::Format::RGB565, u16>(*(src_ptr++));
}

template<>
ALWAYS_INLINE void CopyOutRow16<GPUTexture::Format::RGBA8, u32>(const u16* src_ptr, u32* dst_ptr, u32 width)
{
  u32 col = 0;

  const u32 aligned_width = Common::AlignDownPow2(width, 8);
  for (; col < aligned_width; col += 8)
  {
    constexpr GSVector4i single_mask = GSVector4i::cxpr16(0x1F);
    const GSVector4i value = GSVector4i::load<false>(src_ptr);
    src_ptr += 8;
    const GSVector4i red = (value & single_mask).sll16<3>();
    const GSVector4i green = (value.srl16<5>() & single_mask).sll16<3>();
    const GSVector4i blue = (value.srl16<10>() & single_mask).sll16<3>();
    const GSVector4i alpha_high = value.sra16<15>().sll16<8>();
    const GSVector4i low_pair = red | green.sll16<8>();
    const GSVector4i high_pair = blue | alpha_high;
    GSVector4i::store<false>(dst_ptr, low_pair.upl16(high_pair));
    GSVector4i::store<false>(dst_ptr + 4, low_pair.uph16(high_pair));
    dst_ptr += 8;
  }

  for (; col < width; col++)
    *(dst_ptr++) = VRAM16ToOutput<GPUTexture::Format::RGBA8, u32>(*(src_ptr++));
}

template<>
ALWAYS_INLINE void CopyOutRow16<GPUTexture::Format::BGRA8, u32>(const u16* src_ptr, u32* dst_ptr, u32 width)
{
  u32 col = 0;

  const u32 aligned_width = Common::AlignDownPow2(width, 8);
  for (; col < aligned_width; col += 8)
  {
    constexpr GSVector4i single_mask = GSVector4i::cxpr16(0x1F);
    const GSVector4i value = GSVector4i::load<false>(src_ptr);
    src_ptr += 8;
    const GSVector4i red = (value & single_mask).sll16<3>();
    const GSVector4i green = (value.srl16<5>() & single_mask).sll16<3>();
    const GSVector4i blue = (value.srl16<10>() & single_mask).sll16<3>();
    const GSVector4i low_pair = blue | green.sll16<8>();
    const GSVector4i high_pair = red | GSVector4i::cxpr16(static_cast<s16>(0xFF00));
    GSVector4i::store<false>(dst_ptr, low_pair.upl16(high_pair));
    GSVector4i::store<false>(dst_ptr + 4, low_pair.uph16(high_pair));
    dst_ptr += 8;
  }

  for (; col < width; col++)
    *(dst_ptr++) = VRAM16ToOutput<GPUTexture::Format::BGRA8, u32>(*(src_ptr++));
}

template<GPUTexture::Format display_format>
ALWAYS_INLINE_RELEASE bool GPU_SW::CopyOut15Bit(u32 src_x, u32 src_y, u32 width, u32 height, u32 line_skip)
{
  using OutputPixelType =
    std::conditional_t<display_format == GPUTexture::Format::RGBA8 || display_format == GPUTexture::Format::BGRA8, u32,
                       u16>;

  GPUTexture* texture = GetDisplayTexture(width, height, display_format);
  if (!texture) [[unlikely]]
    return false;

  u32 dst_stride = width * sizeof(OutputPixelType);
  u8* dst_ptr = m_upload_buffer.data();
  const bool mapped = texture->Map(reinterpret_cast<void**>(&dst_ptr), &dst_stride, 0, 0, width, height);

  // Fast path when not wrapping around.
  if ((src_x + width) <= VRAM_WIDTH && (src_y + (height << line_skip)) <= VRAM_HEIGHT)
  {
    const u16* src_ptr = &g_vram[src_y * VRAM_WIDTH + src_x];
    const u32 src_step = VRAM_WIDTH << line_skip;
    for (u32 row = 0; row < height; row++)
    {
      CopyOutRow16<display_format>(src_ptr, reinterpret_cast<OutputPixelType*>(dst_ptr), width);
      src_ptr += src_step;
      dst_ptr += dst_stride;
    }
  }
  else
  {
    const u32 end_x = src_x + width;
    const u32 y_step = (1 << line_skip);
    for (u32 row = 0; row < height; row++)
    {
      const u16* src_row_ptr = &g_vram[(src_y % VRAM_HEIGHT) * VRAM_WIDTH];
      OutputPixelType* dst_row_ptr = reinterpret_cast<OutputPixelType*>(dst_ptr);

      for (u32 col = src_x; col < end_x; col++)
        *(dst_row_ptr++) = VRAM16ToOutput<display_format, OutputPixelType>(src_row_ptr[col % VRAM_WIDTH]);

      src_y += y_step;
      dst_ptr += dst_stride;
    }
  }

  if (mapped)
    texture->Unmap();
  else
    texture->Update(0, 0, width, height, m_upload_buffer.data(), dst_stride);

  return true;
}

template<GPUTexture::Format display_format>
ALWAYS_INLINE_RELEASE bool GPU_SW::CopyOut24Bit(u32 src_x, u32 src_y, u32 skip_x, u32 width, u32 height, u32 line_skip)
{
  using OutputPixelType =
    std::conditional_t<display_format == GPUTexture::Format::RGBA8 || display_format == GPUTexture::Format::BGRA8, u32,
                       u16>;

  GPUTexture* texture = GetDisplayTexture(width, height, display_format);
  if (!texture) [[unlikely]]
    return false;

  u32 dst_stride = Common::AlignUpPow2<u32>(width * sizeof(OutputPixelType), 4);
  u8* dst_ptr = m_upload_buffer.data();
  const bool mapped = texture->Map(reinterpret_cast<void**>(&dst_ptr), &dst_stride, 0, 0, width, height);

  if ((src_x + width) <= VRAM_WIDTH && (src_y + (height << line_skip)) <= VRAM_HEIGHT)
  {
    const u8* src_ptr = reinterpret_cast<const u8*>(&g_vram[src_y * VRAM_WIDTH + src_x]) + (skip_x * 3);
    const u32 src_stride = (VRAM_WIDTH << line_skip) * sizeof(u16);
    for (u32 row = 0; row < height; row++)
    {
      if constexpr (display_format == GPUTexture::Format::RGBA8)
      {
        const u8* src_row_ptr = src_ptr;
        u8* dst_row_ptr = reinterpret_cast<u8*>(dst_ptr);
        for (u32 col = 0; col < width; col++)
        {
          *(dst_row_ptr++) = *(src_row_ptr++);
          *(dst_row_ptr++) = *(src_row_ptr++);
          *(dst_row_ptr++) = *(src_row_ptr++);
          *(dst_row_ptr++) = 0xFF;
        }
      }
      else if constexpr (display_format == GPUTexture::Format::BGRA8)
      {
        const u8* src_row_ptr = src_ptr;
        u8* dst_row_ptr = reinterpret_cast<u8*>(dst_ptr);
        for (u32 col = 0; col < width; col++)
        {
          *(dst_row_ptr++) = src_row_ptr[2];
          *(dst_row_ptr++) = src_row_ptr[1];
          *(dst_row_ptr++) = src_row_ptr[0];
          *(dst_row_ptr++) = 0xFF;
          src_row_ptr += 3;
        }
      }
      else if constexpr (display_format == GPUTexture::Format::RGB565)
      {
        const u8* src_row_ptr = src_ptr;
        u16* dst_row_ptr = reinterpret_cast<u16*>(dst_ptr);
        for (u32 col = 0; col < width; col++)
        {
          *(dst_row_ptr++) = ((static_cast<u16>(src_row_ptr[0]) >> 3) << 11) |
                             ((static_cast<u16>(src_row_ptr[1]) >> 2) << 5) | (static_cast<u16>(src_row_ptr[2]) >> 3);
          src_row_ptr += 3;
        }
      }
      else if constexpr (display_format == GPUTexture::Format::RGBA5551)
      {
        const u8* src_row_ptr = src_ptr;
        u16* dst_row_ptr = reinterpret_cast<u16*>(dst_ptr);
        for (u32 col = 0; col < width; col++)
        {
          *(dst_row_ptr++) = ((static_cast<u16>(src_row_ptr[0]) >> 3) << 10) |
                             ((static_cast<u16>(src_row_ptr[1]) >> 3) << 5) | (static_cast<u16>(src_row_ptr[2]) >> 3);
          src_row_ptr += 3;
        }
      }

      src_ptr += src_stride;
      dst_ptr += dst_stride;
    }
  }
  else
  {
    const u32 y_step = (1 << line_skip);

    for (u32 row = 0; row < height; row++)
    {
      const u16* src_row_ptr = &g_vram[(src_y % VRAM_HEIGHT) * VRAM_WIDTH];
      OutputPixelType* dst_row_ptr = reinterpret_cast<OutputPixelType*>(dst_ptr);

      for (u32 col = 0; col < width; col++)
      {
        const u32 offset = (src_x + (((skip_x + col) * 3) / 2));
        const u16 s0 = src_row_ptr[offset % VRAM_WIDTH];
        const u16 s1 = src_row_ptr[(offset + 1) % VRAM_WIDTH];
        const u8 shift = static_cast<u8>(col & 1u) * 8;
        const u32 rgb = (((ZeroExtend32(s1) << 16) | ZeroExtend32(s0)) >> shift);

        if constexpr (display_format == GPUTexture::Format::RGBA8)
        {
          *(dst_row_ptr++) = rgb | 0xFF000000u;
        }
        else if constexpr (display_format == GPUTexture::Format::BGRA8)
        {
          *(dst_row_ptr++) = (rgb & 0x00FF00) | ((rgb & 0xFF) << 16) | ((rgb >> 16) & 0xFF) | 0xFF000000u;
        }
        else if constexpr (display_format == GPUTexture::Format::RGB565)
        {
          *(dst_row_ptr++) = ((rgb >> 3) & 0x1F) | (((rgb >> 10) << 5) & 0x7E0) | (((rgb >> 19) << 11) & 0x3E0000);
        }
        else if constexpr (display_format == GPUTexture::Format::RGBA5551)
        {
          *(dst_row_ptr++) = ((rgb >> 3) & 0x1F) | (((rgb >> 11) << 5) & 0x3E0) | (((rgb >> 19) << 10) & 0x1F0000);
        }
      }

      src_y += y_step;
      dst_ptr += dst_stride;
    }
  }

  if (mapped)
    texture->Unmap();
  else
    texture->Update(0, 0, width, height, m_upload_buffer.data(), dst_stride);

  return true;
}

bool GPU_SW::CopyOut(u32 src_x, u32 src_y, u32 skip_x, u32 width, u32 height, u32 line_skip, bool is_24bit)
{
  if (!is_24bit)
  {
    DebugAssert(skip_x == 0);

    switch (m_16bit_display_format)
    {
      case GPUTexture::Format::RGBA5551:
        return CopyOut15Bit<GPUTexture::Format::RGBA5551>(src_x, src_y, width, height, line_skip);

      case GPUTexture::Format::RGB565:
        return CopyOut15Bit<GPUTexture::Format::RGB565>(src_x, src_y, width, height, line_skip);

      case GPUTexture::Format::RGBA8:
        return CopyOut15Bit<GPUTexture::Format::RGBA8>(src_x, src_y, width, height, line_skip);

      case GPUTexture::Format::BGRA8:
        return CopyOut15Bit<GPUTexture::Format::BGRA8>(src_x, src_y, width, height, line_skip);

      default:
        UnreachableCode();
    }
  }
  else
  {
    switch (m_24bit_display_format)
    {
      case GPUTexture::Format::RGBA5551:
        return CopyOut24Bit<GPUTexture::Format::RGBA5551>(src_x, src_y, skip_x, width, height, line_skip);

      case GPUTexture::Format::RGB565:
        return CopyOut24Bit<GPUTexture::Format::RGB565>(src_x, src_y, skip_x, width, height, line_skip);

      case GPUTexture::Format::RGBA8:
        return CopyOut24Bit<GPUTexture::Format::RGBA8>(src_x, src_y, skip_x, width, height, line_skip);

      case GPUTexture::Format::BGRA8:
        return CopyOut24Bit<GPUTexture::Format::BGRA8>(src_x, src_y, skip_x, width, height, line_skip);

      default:
        UnreachableCode();
    }
  }
}

void GPU_SW::UpdateDisplay()
{
  // fill display texture
  m_backend->Sync(true);
  m_presented_display_state = GetDisplayState();

  if (!g_settings.debugging.show_vram)
  {
    if (IsDisplayDisabled())
    {
      ClearDisplayTexture();
      return;
    }

    const bool is_24bit = m_GPUSTAT.display_area_color_depth_24;
    const bool interlaced = IsInterlacedDisplayEnabled();
    const u32 field = GetInterlacedDisplayField();
    const u32 vram_offset_x = is_24bit ? m_crtc_state.regs.X : m_crtc_state.display_vram_left;
    const u32 vram_offset_y =
      m_crtc_state.display_vram_top + ((interlaced && m_GPUSTAT.vertical_resolution) ? field : 0);
    const u32 skip_x = is_24bit ? (m_crtc_state.display_vram_left - m_crtc_state.regs.X) : 0;
    const u32 read_width = m_crtc_state.display_vram_width;
    const u32 read_height = interlaced ? (m_crtc_state.display_vram_height / 2) : m_crtc_state.display_vram_height;

    if (IsInterlacedDisplayEnabled())
    {
      const u32 line_skip = m_GPUSTAT.vertical_resolution;
      if (CopyOut(vram_offset_x, vram_offset_y, skip_x, read_width, read_height, line_skip, is_24bit))
      {
        SetDisplayTexture(m_upload_texture.get(), nullptr, 0, 0, read_width, read_height);
        if (is_24bit && g_settings.display_24bit_chroma_smoothing)
        {
          if (ApplyChromaSmoothing())
            Deinterlace(field, 0);
        }
        else
        {
          Deinterlace(field, 0);
        }
      }
    }
    else
    {
      if (CopyOut(vram_offset_x, vram_offset_y, skip_x, read_width, read_height, 0, is_24bit))
      {
        SetDisplayTexture(m_upload_texture.get(), nullptr, 0, 0, read_width, read_height);
        if (is_24bit && g_settings.display_24bit_chroma_smoothing)
          ApplyChromaSmoothing();
      }
    }
  }
  else
  {
    if (CopyOut(0, 0, 0, VRAM_WIDTH, VRAM_HEIGHT, 0, false))
      SetDisplayTexture(m_upload_texture.get(), nullptr, 0, 0, VRAM_WIDTH, VRAM_HEIGHT);
  }
}

void GPU_SW::FlushRender()
{
}

std::unique_ptr<GPU> GPU::CreateSoftwareRenderer()
{
  std::unique_ptr<GPU_SW> gpu(std::make_unique<GPU_SW>());
  if (!gpu->Initialize())
    return nullptr;

  return gpu;
}
