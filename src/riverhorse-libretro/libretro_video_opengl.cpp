// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "libretro_video_opengl.h"
#include "libretro_crosshair.h"

#include "core/gpu.h"
#include "core/settings.h"

#include "util/gpu_device.h"
#include "util/opengl_texture.h"

#include "common/log.h"

#include "glad/gl.h"

Log_SetChannel(LibretroOpenGL);

namespace LibretroOpenGL {
static retro_hw_render_callback s_hw_render = {};
static std::unique_ptr<GPUTexture> s_frame_texture;
static GLuint s_frame_fbo = 0;
} // namespace LibretroOpenGL

bool LibretroOpenGL::Setup(retro_environment_t environment, retro_hw_context_reset_t context_reset,
                           retro_hw_context_reset_t context_destroy)
{
  retro_hw_context_type preferred = RETRO_HW_CONTEXT_NONE;
  environment(RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER, &preferred);

  bool gles;
  switch (preferred)
  {
    case RETRO_HW_CONTEXT_NONE:
    case RETRO_HW_CONTEXT_OPENGL:
    case RETRO_HW_CONTEXT_OPENGL_CORE:
      gles = false;
      break;
    case RETRO_HW_CONTEXT_OPENGLES3:
    case RETRO_HW_CONTEXT_OPENGLES_VERSION:
      gles = true;
      break;
    default:
      return false;
  }

  environment(RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT, nullptr);

  const auto request_context = [&](bool use_gles) {
    s_hw_render = {};
    s_hw_render.context_type = use_gles ? RETRO_HW_CONTEXT_OPENGLES3 : RETRO_HW_CONTEXT_OPENGL_CORE;
    s_hw_render.version_major = 3;
    s_hw_render.version_minor = use_gles ? 0 : 3;
    s_hw_render.context_reset = context_reset;
    s_hw_render.context_destroy = context_destroy;
    s_hw_render.bottom_left_origin = true;
    return environment(RETRO_ENVIRONMENT_SET_HW_RENDER, &s_hw_render);
  };
  if (!request_context(gles))
  {
    if (gles || !request_context(true))
      return false;
    gles = true;
  }

  OpenGLContext::SetHostContextFactory([gles](const WindowInfo& wi, Error* error) -> std::unique_ptr<OpenGLContext> {
    return std::make_unique<LibretroOpenGLContext>(wi, s_hw_render.get_proc_address, gles);
  });
  return true;
}

void LibretroOpenGL::Shutdown()
{
  OpenGLContext::SetHostContextFactory(nullptr);
}

void LibretroOpenGL::DestroyFrameResources()
{
  if (s_frame_texture)
    g_gpu_device->RecycleTexture(std::move(s_frame_texture));

  if (s_frame_fbo != 0)
  {
    glDeleteFramebuffers(1, &s_frame_fbo);
    s_frame_fbo = 0;
  }
}

bool LibretroOpenGL::RenderFrame(u32* width, u32* height)
{
  *width = g_gpu->GetDisplayTextureViewWidth();
  *height = g_gpu->GetDisplayTextureViewHeight();
  if (*width == 0 || *height == 0)
    return false;

  if (!s_frame_texture || s_frame_texture->GetWidth() != *width || s_frame_texture->GetHeight() != *height)
  {
    if (s_frame_texture)
      g_gpu_device->RecycleTexture(std::move(s_frame_texture));
    s_frame_texture =
      g_gpu_device->FetchTexture(*width, *height, 1, 1, 1, GPUTexture::Type::RenderTarget, GPUTexture::Format::RGBA8);
    if (!s_frame_texture)
    {
      ERROR_LOG("Failed to create a {}x{} frame texture.", *width, *height);
      return false;
    }
  }

  if (!g_gpu->RenderDisplayAtInternalResolution(s_frame_texture.get(), true))
    return false;
  LibretroCrosshair::Draw(s_frame_texture.get());

  if (s_frame_fbo == 0)
    glGenFramebuffers(1, &s_frame_fbo);

  GLint previous_read_fbo, previous_draw_fbo;
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read_fbo);
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous_draw_fbo);
  const GLboolean scissor_enabled = glIsEnabled(GL_SCISSOR_TEST);

  glBindFramebuffer(GL_READ_FRAMEBUFFER, s_frame_fbo);
  glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         static_cast<OpenGLTexture*>(s_frame_texture.get())->GetGLId(), 0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(s_hw_render.get_current_framebuffer()));
  glDisable(GL_SCISSOR_TEST);
  glBlitFramebuffer(0, 0, *width, *height, 0, 0, *width, *height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

  if (scissor_enabled)
    glEnable(GL_SCISSOR_TEST);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, previous_read_fbo);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, previous_draw_fbo);
  return true;
}

LibretroOpenGLContext::LibretroOpenGLContext(const WindowInfo& wi, retro_hw_get_proc_address_t get_proc_address,
                                             bool gles)
  : OpenGLContext(wi), m_get_proc_address(get_proc_address)
{
  m_version = gles ? Version{Profile::ES, 3, 0} : Version{Profile::Core, 3, 3};
}

LibretroOpenGLContext::~LibretroOpenGLContext() = default;

void* LibretroOpenGLContext::GetProcAddress(const char* name)
{
  return reinterpret_cast<void*>(m_get_proc_address(name));
}

bool LibretroOpenGLContext::ChangeSurface(const WindowInfo& new_wi)
{
  m_wi = new_wi;
  return true;
}

void LibretroOpenGLContext::ResizeSurface(u32 new_surface_width, u32 new_surface_height)
{
}

bool LibretroOpenGLContext::SwapBuffers()
{
  return true;
}

bool LibretroOpenGLContext::IsCurrent() const
{
  return true;
}

bool LibretroOpenGLContext::MakeCurrent()
{
  return true;
}

bool LibretroOpenGLContext::DoneCurrent()
{
  return true;
}

bool LibretroOpenGLContext::SupportsNegativeSwapInterval() const
{
  return false;
}

bool LibretroOpenGLContext::SetSwapInterval(s32 interval)
{
  return true;
}

std::unique_ptr<OpenGLContext> LibretroOpenGLContext::CreateSharedContext(const WindowInfo& wi, Error* error)
{
  return nullptr;
}
