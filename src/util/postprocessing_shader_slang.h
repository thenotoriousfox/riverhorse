// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "postprocessing_shader.h"

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class Error;

namespace PostProcessing {

/// A multi-pass shader preset in the libretro slang format (.slangp).
class SlangShader final : public Shader
{
public:
  SlangShader();
  ~SlangShader() override;

  bool IsValid() const override;
  bool WantsDepthBuffer() const override;

  bool LoadFromFile(std::string name, const std::string& preset_path, Error* error);

  bool ResizeOutput(GPUTexture::Format format, u32 width, u32 height) override;
  bool CompilePipeline(GPUTexture::Format format, u32 width, u32 height, ProgressCallback* progress) override;
  bool Apply(GPUTexture* input_color, GPUTexture* input_depth, GPUTexture* final_target, GSVector4i final_rect,
             s32 orig_width, s32 orig_height, s32 native_width, s32 native_height, u32 target_width,
             u32 target_height) override;

private:
  enum class ScaleType : u8
  {
    Source,
    Viewport,
    Absolute,
  };

  enum class TextureKind : u8
  {
    Original,
    Source,
    OriginalHistory,
    PassOutput,
    PassFeedback,
    Lut,
  };

  struct TextureRef
  {
    TextureKind kind;
    u32 index;
  };

  enum class UniformKind : u8
  {
    MVP,
    OutputSize,
    FinalViewportSize,
    FrameCount,
    FrameDirection,
    TextureSize,
    Parameter,
  };

  struct Uniform
  {
    UniformKind kind;
    TextureRef texture;
    u32 option_index;
    u32 offset;
    u32 components;
    bool is_integer;
  };

  struct SamplerBinding
  {
    TextureRef texture;
    u32 slot;
  };

  struct Lut
  {
    std::string name;
    std::string path;
    GPUSampler::Config sampler;
    std::unique_ptr<GPUTexture> texture;
  };

  struct Pass
  {
    std::string shader_path;
    std::string alias;
    std::string vertex_source;
    std::string fragment_source;
    ScaleType scale_type_x = ScaleType::Source;
    ScaleType scale_type_y = ScaleType::Source;
    float scale_x = 1.0f;
    float scale_y = 1.0f;
    u32 frame_count_mod = 0;
    GPUTexture::Format format = GPUTexture::Format::RGBA8;
    GPUSampler::Config sampler;
    bool feedback_used = false;

    std::vector<Uniform> uniforms;
    u32 uniforms_size = 0;
    std::vector<SamplerBinding> samplers;
    std::unique_ptr<GPUPipeline> pipeline;
    std::unique_ptr<GPUTexture> output;
    std::unique_ptr<GPUTexture> feedback;
  };

  struct UniformMember
  {
    std::string type;
    std::string name;
    u32 offset;
    u32 components;
    bool is_integer;
  };

  bool LoadPreset(const std::string& preset_path, Error* error);
  bool LoadPassSource(Pass& pass, std::string* declarations, Error* error);
  bool ProcessPassSource(Pass& pass, u32 pass_index, std::string source, Error* error);
  std::optional<TextureRef> LookupTexture(std::string_view name, u32 pass_index) const;
  void NoteTextureUse(const TextureRef& ref);
  std::optional<u32> FindOption(std::string_view name) const;

  bool CreateCropPipeline(Error* error);
  bool CropOriginal(GPUTexture* input_color, GSVector4i src_rect, u32 target_width, u32 target_height,
                    s32 orig_width, s32 orig_height);
  GPUTexture* GetTexture(const TextureRef& ref, u32 pass_index) const;
  void FillUniforms(const Pass& pass, u32 pass_index, u8* buffer, u32 output_width, u32 output_height,
                    const GSVector4i& final_rect) const;
  bool EnsureTexture(std::unique_ptr<GPUTexture>& texture, u32 width, u32 height, GPUTexture::Format format);

  std::vector<Pass> m_passes;
  std::vector<Lut> m_luts;
  u32 m_history_size = 0;
  u32 m_frame_count = 0;
  bool m_valid = false;

  std::unique_ptr<GPUPipeline> m_crop_pipeline;
  std::unique_ptr<GPUTexture> m_original;
  std::deque<std::unique_ptr<GPUTexture>> m_history;
};

} // namespace PostProcessing
