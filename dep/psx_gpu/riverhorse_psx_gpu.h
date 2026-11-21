// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rh_psx_gpu rh_psx_gpu;

rh_psx_gpu* rh_psx_gpu_create(uint16_t* vram);
void rh_psx_gpu_destroy(rh_psx_gpu* gpu);
void rh_psx_gpu_parse(rh_psx_gpu* gpu, uint32_t* list, uint32_t word_count, uint32_t* ex_regs);
void rh_psx_gpu_flush(rh_psx_gpu* gpu);
void rh_psx_gpu_invalidate_region(rh_psx_gpu* gpu, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2);
void rh_psx_gpu_set_interlace(rh_psx_gpu* gpu, int enable, int draw_odd_lines);

#ifdef __cplusplus
}
#endif
