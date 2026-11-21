// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "riverhorse_psx_gpu.h"

#include <stdlib.h>
#ifdef _WIN32
#include <malloc.h>
#endif

static void* aligned_gpu_alloc(size_t alignment, size_t size)
{
#ifdef _WIN32
  return _aligned_malloc(size, alignment);
#else
  return aligned_alloc(alignment, size);
#endif
}

static void aligned_gpu_free(void* ptr)
{
#ifdef _WIN32
  _aligned_free(ptr);
#else
  free(ptr);
#endif
}

static void sync_enhancement_buffers(int x, int y, int w, int h)
{
  (void)x;
  (void)y;
  (void)w;
  (void)h;
}

#include "plugins/gpu_neon/psx_gpu/psx_gpu.c"
#include "plugins/gpu_neon/psx_gpu/psx_gpu_parse.c"

rh_psx_gpu* rh_psx_gpu_create(uint16_t* vram)
{
  psx_gpu_struct* gpu = aligned_gpu_alloc(256, (sizeof(psx_gpu_struct) + 255) & ~(size_t)255);
  if (!gpu)
    return NULL;

  memset(gpu, 0, sizeof(*gpu));
  initialize_psx_gpu(gpu, vram);
  return (rh_psx_gpu*)gpu;
}

void rh_psx_gpu_destroy(rh_psx_gpu* handle)
{
  aligned_gpu_free(handle);
}

void rh_psx_gpu_parse(rh_psx_gpu* handle, uint32_t* list, uint32_t word_count, uint32_t* ex_regs)
{
  psx_gpu_struct* gpu = (psx_gpu_struct*)handle;
  s32 cycles_sum = 0, cycles_last = 0;
  u32 last_command = 0;
  gpu_parse(gpu, list, word_count * 4, ex_regs, &cycles_sum, &cycles_last, &last_command);
}

void rh_psx_gpu_flush(rh_psx_gpu* handle)
{
  psx_gpu_struct* gpu = (psx_gpu_struct*)handle;
  flush_render_block_buffer(gpu);
}

void rh_psx_gpu_invalidate_region(rh_psx_gpu* handle, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2)
{
  psx_gpu_struct* gpu = (psx_gpu_struct*)handle;
  update_texture_cache_region(gpu, x1, y1, x2, y2);
}

void rh_psx_gpu_set_interlace(rh_psx_gpu* handle, int enable, int draw_odd_lines)
{
  psx_gpu_struct* gpu = (psx_gpu_struct*)handle;
  gpu->render_mode &= ~(RENDER_INTERLACE_ENABLED | RENDER_INTERLACE_ODD);
  if (enable)
    gpu->render_mode |= RENDER_INTERLACE_ENABLED;
  if (draw_odd_lines)
    gpu->render_mode |= RENDER_INTERLACE_ODD;
}
