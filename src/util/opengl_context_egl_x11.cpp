// SPDX-FileCopyrightText: 2019-2024 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: (GPL-3.0 OR CC-BY-NC-ND-4.0)

#include "opengl_context_egl_x11.h"

#include "common/error.h"

#include <X11/Xlib.h>

OpenGLContextEGLX11::OpenGLContextEGLX11(const WindowInfo& wi) : OpenGLContextEGL(wi)
{
}

OpenGLContextEGLX11::~OpenGLContextEGLX11() = default;

std::unique_ptr<OpenGLContext> OpenGLContextEGLX11::Create(const WindowInfo& wi,
                                                           std::span<const Version> versions_to_try, Error* error)
{
  std::unique_ptr<OpenGLContextEGLX11> context = std::make_unique<OpenGLContextEGLX11>(wi);
  if (!context->Initialize(versions_to_try, error))
    return nullptr;

  return context;
}

std::unique_ptr<OpenGLContext> OpenGLContextEGLX11::CreateSharedContext(const WindowInfo& wi, Error* error)
{
  std::unique_ptr<OpenGLContextEGLX11> context = std::make_unique<OpenGLContextEGLX11>(wi);
  context->m_display = m_display;

  if (!context->CreateContextAndSurface(m_version, m_context, false))
    return nullptr;

  return context;
}

EGLDisplay OpenGLContextEGLX11::GetPlatformDisplay(Error* error)
{
  EGLDisplay dpy = TryGetPlatformDisplay(EGL_PLATFORM_X11_KHR, "EGL_EXT_platform_x11");
  if (dpy == EGL_NO_DISPLAY)
    dpy = GetFallbackDisplay(error);

  return dpy;
}

EGLSurface OpenGLContextEGLX11::CreatePlatformSurface(EGLConfig config, void* win, Error* error)
{
  // This is hideous.. the EXT version requires a pointer to the window, whereas the base
  // version requires the window itself, casted to void*...
  EGLSurface surface = TryCreatePlatformSurface(config, &win, error);
  if (surface == EGL_NO_SURFACE)
    surface = CreateFallbackSurface(config, win, error);

  return surface;
}
bool OpenGLContextEGLX11::IsConfigCompatibleWithWindow(EGLConfig config)
{
  EGLint visual_id;
  if (!eglGetConfigAttrib(m_display, config, EGL_NATIVE_VISUAL_ID, &visual_id) || visual_id == 0)
    return true;

  XWindowAttributes attributes;
  if (!XGetWindowAttributes(static_cast<Display*>(m_wi.display_connection),
                            static_cast<Window>(reinterpret_cast<uintptr_t>(m_wi.window_handle)), &attributes))
  {
    return true;
  }

  return (static_cast<VisualID>(visual_id) == XVisualIDFromVisual(attributes.visual));
}
