// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "libretro_crosshair.h"
#include "libretro_input.h"

#include "util/gpu_device.h"
#include "util/shadergen.h"

#include "common/error.h"
#include "common/log.h"

#include <algorithm>
#include <array>
#include <memory>

Log_SetChannel(LibretroCrosshair);

namespace LibretroCrosshair {
namespace {
struct Bar
{
  s32 left;
  s32 top;
  s32 right;
  s32 bottom;
  u32 rgb;
};
} // namespace

static constexpr std::array<u32, 4> PLAYER_COLORS = {0xFF3030u, 0x3080FFu, 0x30E030u, 0xFFE030u};
static constexpr u32 OUTLINE_COLOR = 0x000000u;

static std::vector<Bar> GetBars(u32 width, u32 height);
static bool CreatePipeline();

static std::unique_ptr<GPUPipeline> s_pipeline;
static bool s_pipeline_failed = false;

} // namespace LibretroCrosshair

std::vector<LibretroCrosshair::Bar> LibretroCrosshair::GetBars(u32 width, u32 height)
{
  std::vector<Bar> bars;
  const s32 arm = std::max<s32>(static_cast<s32>(height) / 40, 4);
  const s32 half_thickness = std::max<s32>(static_cast<s32>(height) / 480, 1);
  for (const LibretroInput::Crosshair& crosshair : LibretroInput::GetCrosshairs())
  {
    const s32 x = static_cast<s32>(crosshair.x * static_cast<float>(width));
    const s32 y = static_cast<s32>(crosshair.y * static_cast<float>(height));
    const u32 rgb = PLAYER_COLORS[crosshair.player % PLAYER_COLORS.size()];
    for (const s32 outline : {half_thickness, 0})
    {
      const u32 color = outline ? OUTLINE_COLOR : rgb;
      bars.push_back(Bar{x - arm - outline, y - half_thickness - outline, x + arm + 1 + outline,
                         y + half_thickness + 1 + outline, color});
      bars.push_back(Bar{x - half_thickness - outline, y - arm - outline, x + half_thickness + 1 + outline,
                         y + arm + 1 + outline, color});
    }
  }

  for (Bar& bar : bars)
  {
    bar.left = std::clamp<s32>(bar.left, 0, static_cast<s32>(width));
    bar.right = std::clamp<s32>(bar.right, 0, static_cast<s32>(width));
    bar.top = std::clamp<s32>(bar.top, 0, static_cast<s32>(height));
    bar.bottom = std::clamp<s32>(bar.bottom, 0, static_cast<s32>(height));
  }
  return bars;
}

bool LibretroCrosshair::CreatePipeline()
{
  if (s_pipeline)
    return true;
  if (s_pipeline_failed)
    return false;

  ShaderGen shadergen(g_gpu_device->GetRenderAPI(), ShaderGen::GetShaderLanguageForAPI(g_gpu_device->GetRenderAPI()),
                      g_gpu_device->GetFeatures().dual_source_blend, g_gpu_device->GetFeatures().framebuffer_fetch);
  Error error;
  std::unique_ptr<GPUShader> vs = g_gpu_device->CreateShader(GPUShaderStage::Vertex, shadergen.GetLanguage(),
                                                             shadergen.GenerateScreenQuadVertexShader(), &error);
  std::unique_ptr<GPUShader> fs = vs ? g_gpu_device->CreateShader(GPUShaderStage::Fragment, shadergen.GetLanguage(),
                                                                  shadergen.GenerateFillFragmentShader(), &error) :
                                       nullptr;

  GPUPipeline::GraphicsConfig plconfig = {};
  plconfig.layout = GPUPipeline::Layout::SingleTextureAndPushConstants;
  plconfig.primitive = GPUPipeline::Primitive::Triangles;
  plconfig.input_layout.vertex_stride = 0;
  plconfig.rasterization = GPUPipeline::RasterizationState::GetNoCullState();
  plconfig.depth = GPUPipeline::DepthState::GetNoTestsState();
  plconfig.blend = GPUPipeline::BlendState::GetNoBlendingState();
  plconfig.SetTargetFormats(GPUTexture::Format::RGBA8);
  plconfig.samples = 1;
  plconfig.vertex_shader = vs.get();
  plconfig.fragment_shader = fs.get();
  if (fs)
    s_pipeline = g_gpu_device->CreatePipeline(plconfig, &error);

  if (!s_pipeline)
  {
    ERROR_LOG("Failed to create the crosshair pipeline: {}", error.GetDescription());
    s_pipeline_failed = true;
    return false;
  }

  return true;
}

void LibretroCrosshair::Draw(GPUTexture* target)
{
  const std::vector<Bar> bars = GetBars(target->GetWidth(), target->GetHeight());
  if (bars.empty() || !CreatePipeline())
    return;

  g_gpu_device->SetRenderTarget(target);
  g_gpu_device->SetPipeline(s_pipeline.get());
  for (const Bar& bar : bars)
  {
    if (bar.right <= bar.left || bar.bottom <= bar.top)
      continue;

    const float color[4] = {static_cast<float>((bar.rgb >> 16) & 0xFFu) / 255.0f,
                            static_cast<float>((bar.rgb >> 8) & 0xFFu) / 255.0f,
                            static_cast<float>(bar.rgb & 0xFFu) / 255.0f, 1.0f};
    g_gpu_device->PushUniformBuffer(color, sizeof(color));
    g_gpu_device->SetViewportAndScissor(GSVector4i(bar.left, bar.top, bar.right, bar.bottom));
    g_gpu_device->Draw(3, 0);
  }
}

void LibretroCrosshair::Draw(u32* xrgb_pixels, u32 width, u32 height)
{
  for (const Bar& bar : GetBars(width, height))
  {
    for (s32 y = bar.top; y < bar.bottom; y++)
      std::fill(xrgb_pixels + y * width + bar.left, xrgb_pixels + y * width + bar.right, bar.rgb);
  }
}

void LibretroCrosshair::DestroyResources()
{
  s_pipeline.reset();
  s_pipeline_failed = false;
}
