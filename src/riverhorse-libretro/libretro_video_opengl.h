// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "util/opengl_context.h"

#include "libretro.h"

namespace LibretroOpenGL {

bool Setup(retro_environment_t environment, retro_hw_context_reset_t context_reset,
           retro_hw_context_reset_t context_destroy);
void Shutdown();
bool RenderFrame(u32* width, u32* height);
void DestroyFrameResources();

} // namespace LibretroOpenGL

class LibretroOpenGLContext final : public OpenGLContext
{
public:
  LibretroOpenGLContext(const WindowInfo& wi, retro_hw_get_proc_address_t get_proc_address, bool gles);
  ~LibretroOpenGLContext() override;

  void* GetProcAddress(const char* name) override;
  bool ChangeSurface(const WindowInfo& new_wi) override;
  void ResizeSurface(u32 new_surface_width = 0, u32 new_surface_height = 0) override;
  bool SwapBuffers() override;
  bool IsCurrent() const override;
  bool MakeCurrent() override;
  bool DoneCurrent() override;
  bool SupportsNegativeSwapInterval() const override;
  bool SetSwapInterval(s32 interval) override;
  std::unique_ptr<OpenGLContext> CreateSharedContext(const WindowInfo& wi, Error* error) override;

private:
  retro_hw_get_proc_address_t m_get_proc_address;
};
