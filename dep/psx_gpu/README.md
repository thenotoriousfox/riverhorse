psx_gpu software rasteriser by Gilead Kutnick "Exophase", with NEON and
SIMD paths, taken from pcsx_rearmed (`plugins/gpu_neon/psx_gpu`,
`plugins/gpulib/gpu.h`, `plugins/gpulib/gpu_timing.h`, `plugins/gpulib/prim.c`,
`include/compiler_features.h`, `include/arm_features.h`), licensed GPL
version 2 or later (see COPYING). The files are unmodified; the
directory layout matches pcsx_rearmed so their relative includes work.
`riverhorse_psx_gpu.c`/`.h` are Riverhorse's interface to it.
pcsx_rearmed revision: 7838055e892061f3f5775ff9f6bb3d3d9c7fe742

`win32/sys/time.h` supplies `gettimeofday` for Windows builds, which lack
that header.
