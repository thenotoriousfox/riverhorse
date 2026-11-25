// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "postprocessing_shader_slang.h"
#include "gpu_device.h"
#include "image.h"

#include "common/error.h"
#include "common/file_system.h"
#include "common/log.h"
#include "common/path.h"
#include "common/progress_callback.h"
#include "common/string_util.h"

#include "fmt/format.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <initializer_list>
#include <cstring>
#include <unordered_map>

Log_SetChannel(PostProcessing);

namespace {

static constexpr u32 MAX_INCLUDE_DEPTH = 16;
static constexpr u32 MAX_PASSES = 64;
static constexpr const char* MERGED_BLOCK_INSTANCE = "rh_slang_uniforms";

struct PresetValue
{
  std::string value;
  std::string base_dir;
};
using PresetValues = std::unordered_map<std::string, PresetValue>;

struct UniformBlock
{
  size_t start;
  size_t end;
  std::string body;
  std::string instance;
};

struct SamplerDecl
{
  size_t start;
  size_t end;
  std::string name;
};

struct InputDecl
{
  size_t start;
  size_t end;
  u32 location;
  std::string type;
  std::string name;
};

struct Declarations
{
  std::vector<UniformBlock> blocks;
  std::vector<SamplerDecl> samplers;
  std::vector<InputDecl> inputs;
};

struct Std140Type
{
  u32 size;
  u32 align;
  u32 components;
  bool is_integer;
};

} // namespace

static bool IsIdentifierChar(char ch)
{
  return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

static std::string TrimQuotes(std::string_view value)
{
  value = StringUtil::StripWhitespace(value);
  if (value.size() >= 2 && value.front() == '"')
  {
    const size_t close = value.find('"', 1);
    return std::string(value.substr(1, (close == std::string_view::npos) ? std::string_view::npos : close - 1));
  }

  const size_t comment = value.find('#');
  return std::string(StringUtil::StripWhitespace(value.substr(0, comment)));
}

static std::string ResolvePath(const std::string& base_dir, std::string_view path)
{
  return Path::Canonicalize(Path::IsAbsolute(path) ? std::string(path) : Path::Combine(base_dir, path));
}

static bool ReadPresetFile(const std::string& path, PresetValues* values, u32 depth, Error* error)
{
  if (depth > MAX_INCLUDE_DEPTH)
  {
    Error::SetStringFmt(error, "Too many nested #reference lines at '{}'.", Path::GetFileName(path));
    return false;
  }

  std::optional<std::string> text = FileSystem::ReadFileToString(path.c_str(), error);
  if (!text.has_value())
    return false;

  const std::string base_dir(Path::GetDirectory(path));
  for (std::string_view line : StringUtil::SplitString(text.value(), '\n', false))
  {
    line = StringUtil::StripWhitespace(line);
    if (line.empty())
      continue;

    if (line.starts_with("#reference"))
    {
      if (!ReadPresetFile(ResolvePath(base_dir, TrimQuotes(line.substr(10))), values, depth + 1, error))
        return false;
      continue;
    }

    if (line.front() == '#')
      continue;

    const size_t equals = line.find('=');
    if (equals == std::string_view::npos)
      continue;

    const std::string key(StringUtil::StripWhitespace(line.substr(0, equals)));
    (*values)[key] = PresetValue{TrimQuotes(line.substr(equals + 1)), base_dir};
  }

  return true;
}

static const PresetValue* FindPresetValue(const PresetValues& values, std::string_view key)
{
  const auto it = values.find(std::string(key));
  return (it != values.end()) ? &it->second : nullptr;
}

static bool GetPresetBool(const PresetValues& values, std::string_view key, bool default_value)
{
  const PresetValue* value = FindPresetValue(values, key);
  if (!value)
    return default_value;

  return StringUtil::EqualNoCase(value->value, "true") || value->value == "1";
}

static GPUSampler::AddressMode ParseWrapMode(std::string_view value)
{
  if (value == "clamp_to_edge")
    return GPUSampler::AddressMode::ClampToEdge;
  if (value == "repeat")
    return GPUSampler::AddressMode::Repeat;
  if (value == "mirrored_repeat")
    return GPUSampler::AddressMode::MirrorRepeat;
  return GPUSampler::AddressMode::ClampToBorder;
}

static GPUSampler::Config MakeSamplerConfig(bool linear, GPUSampler::AddressMode wrap)
{
  GPUSampler::Config config = linear ? GPUSampler::GetLinearConfig() : GPUSampler::GetNearestConfig();
  config.address_u = wrap;
  config.address_v = wrap;
  config.address_w = wrap;
  config.border_color = 0xFF000000u;
  return config;
}

static std::optional<GPUTexture::Format> ParseFormatPragma(std::string_view format)
{
  if (format == "R8G8B8A8_UNORM" || format == "R8G8B8A8_SRGB" || format == "B8G8R8A8_UNORM")
    return GPUTexture::Format::RGBA8;
  if (format == "R16G16B16A16_SFLOAT")
    return GPUTexture::Format::RGBA16F;
  if (format == "R32G32B32A32_SFLOAT")
    return GPUTexture::Format::RGBA32F;
  if (format == "R16G16B16A16_UNORM")
    return GPUTexture::Format::RGBA16;
  if (format == "A2B10G10R10_UNORM_PACK32")
    return GPUTexture::Format::RGB10A2;
  return std::nullopt;
}

static bool ReadShaderSource(const std::string& path, std::string* out, u32 depth, Error* error)
{
  if (depth > MAX_INCLUDE_DEPTH)
  {
    Error::SetStringFmt(error, "Too many nested #include lines at '{}'.", Path::GetFileName(path));
    return false;
  }

  std::optional<std::string> text = FileSystem::ReadFileToString(path.c_str(), error);
  if (!text.has_value())
    return false;

  const std::string base_dir(Path::GetDirectory(path));
  for (std::string_view line : StringUtil::SplitString(text.value(), '\n', false))
  {
    if (!line.empty() && line.back() == '\r')
      line.remove_suffix(1);

    const std::string_view stripped = StringUtil::StripWhitespace(line);
    if (stripped.starts_with("#include"))
    {
      if (!ReadShaderSource(ResolvePath(base_dir, TrimQuotes(stripped.substr(8))), out, depth + 1, error))
        return false;
      continue;
    }

    out->append(line);
    out->push_back('\n');
  }

  return true;
}

static std::string StripComments(std::string_view source)
{
  std::string out;
  out.reserve(source.size());
  for (size_t i = 0; i < source.size();)
  {
    if (source.substr(i, 2) == "//")
    {
      while (i < source.size() && source[i] != '\n')
        i++;
    }
    else if (source.substr(i, 2) == "/*")
    {
      i += 2;
      while (i < source.size() && source.substr(i, 2) != "*/")
      {
        if (source[i] == '\n')
          out.push_back('\n');
        i++;
      }
      i += 2;
      out.push_back(' ');
    }
    else
    {
      out.push_back(source[i++]);
    }
  }
  return out;
}

static size_t SkipWhitespace(const std::string& src, size_t pos)
{
  while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos])))
    pos++;
  return pos;
}

static std::string ReadIdentifier(const std::string& src, size_t* pos)
{
  const size_t start = *pos = SkipWhitespace(src, *pos);
  while (*pos < src.size() && IsIdentifierChar(src[*pos]))
    (*pos)++;
  return src.substr(start, *pos - start);
}

static Declarations FindDeclarations(const std::string& src)
{
  Declarations decls;
  for (size_t pos = src.find("layout"); pos != std::string::npos; pos = src.find("layout", pos + 1))
  {
    if ((pos > 0 && IsIdentifierChar(src[pos - 1])) || (pos + 6 < src.size() && IsIdentifierChar(src[pos + 6])))
      continue;

    size_t cursor = SkipWhitespace(src, pos + 6);
    if (cursor >= src.size() || src[cursor] != '(')
      continue;

    const size_t args_end = src.find(')', cursor);
    if (args_end == std::string::npos)
      break;

    const std::string args = src.substr(cursor + 1, args_end - cursor - 1);
    cursor = args_end + 1;
    const std::string storage = ReadIdentifier(src, &cursor);
    if (storage == "uniform")
    {
      const std::string type = ReadIdentifier(src, &cursor);
      cursor = SkipWhitespace(src, cursor);
      if (cursor < src.size() && src[cursor] == '{')
      {
        const size_t body_end = src.find('}', cursor);
        if (body_end == std::string::npos)
          break;

        size_t after = body_end + 1;
        std::string instance = ReadIdentifier(src, &after);
        after = SkipWhitespace(src, after);
        if (after < src.size() && src[after] == ';')
          after++;

        decls.blocks.push_back(
          UniformBlock{pos, after, src.substr(cursor + 1, body_end - cursor - 1), std::move(instance)});
        pos = after - 1;
      }
      else if (type.starts_with("sampler"))
      {
        std::string name = ReadIdentifier(src, &cursor);
        const size_t semicolon = src.find(';', cursor);
        if (semicolon == std::string::npos)
          break;

        decls.samplers.push_back(SamplerDecl{pos, semicolon + 1, std::move(name)});
        pos = semicolon;
      }
    }
    else if (storage == "in")
    {
      const size_t location_pos = args.find("location");
      const size_t equals = args.find('=', (location_pos == std::string::npos) ? 0 : location_pos);
      std::string type = ReadIdentifier(src, &cursor);
      std::string name = ReadIdentifier(src, &cursor);
      const size_t semicolon = src.find(';', cursor);
      if (location_pos == std::string::npos || equals == std::string::npos || semicolon == std::string::npos)
        continue;

      const u32 location =
        StringUtil::FromChars<u32>(StringUtil::StripWhitespace(std::string_view(args).substr(equals + 1))).value_or(0);
      decls.inputs.push_back(InputDecl{pos, semicolon + 1, location, std::move(type), std::move(name)});
      pos = semicolon;
    }
  }

  return decls;
}

static std::optional<Std140Type> GetStd140Type(std::string_view type)
{
  static constexpr const std::pair<std::string_view, Std140Type> types[] = {
    {"float", {4, 4, 1, false}},   {"int", {4, 4, 1, true}},      {"uint", {4, 4, 1, true}},
    {"bool", {4, 4, 1, true}},     {"vec2", {8, 8, 2, false}},    {"ivec2", {8, 8, 2, true}},
    {"uvec2", {8, 8, 2, true}},    {"vec3", {12, 16, 3, false}},  {"ivec3", {12, 16, 3, true}},
    {"uvec3", {12, 16, 3, true}},  {"vec4", {16, 16, 4, false}},  {"ivec4", {16, 16, 4, true}},
    {"uvec4", {16, 16, 4, true}},  {"mat4", {64, 16, 16, false}}, {"mat4x4", {64, 16, 16, false}},
  };
  for (const auto& [name, info] : types)
  {
    if (type == name)
      return info;
  }
  return std::nullopt;
}

static void ReplaceRanges(std::string* text, std::vector<std::pair<std::pair<size_t, size_t>, std::string>> edits)
{
  std::sort(edits.begin(), edits.end(), [](const auto& a, const auto& b) { return a.first.first > b.first.first; });
  for (const auto& [range, replacement] : edits)
    text->replace(range.first, range.second - range.first, replacement);
}

static size_t FindHeaderEnd(const std::string& text)
{
  size_t pos = 0;
  while (pos < text.size())
  {
    const size_t line_end = text.find('\n', pos);
    const std::string_view line =
      StringUtil::StripWhitespace(std::string_view(text).substr(pos, (line_end == std::string::npos) ? std::string::npos :
                                                                                                       line_end - pos));
    if (!line.empty() && !line.starts_with("#version") && !line.starts_with("#extension"))
      break;
    if (line_end == std::string::npos)
      return text.size();
    pos = line_end + 1;
  }
  return pos;
}

PostProcessing::SlangShader::SlangShader() = default;

PostProcessing::SlangShader::~SlangShader() = default;

bool PostProcessing::SlangShader::IsValid() const
{
  return m_valid;
}

bool PostProcessing::SlangShader::WantsDepthBuffer() const
{
  return false;
}

bool PostProcessing::SlangShader::LoadFromFile(std::string name, const std::string& preset_path, Error* error)
{
  m_name = std::move(name);
  return LoadPreset(preset_path, error);
}

bool PostProcessing::SlangShader::LoadPreset(const std::string& preset_path, Error* error)
{
  PresetValues values;
  if (!ReadPresetFile(preset_path, &values, 0, error))
    return false;

  const PresetValue* count_value = FindPresetValue(values, "shaders");
  const u32 pass_count = count_value ? StringUtil::FromChars<u32>(count_value->value).value_or(0) : 0;
  if (pass_count == 0 || pass_count > MAX_PASSES)
  {
    Error::SetStringFmt(error, "'{}' has no valid 'shaders' count.", Path::GetFileName(preset_path));
    return false;
  }

  if (const PresetValue* textures = FindPresetValue(values, "textures"))
  {
    for (const std::string_view lut_name : StringUtil::SplitString(textures->value, ';', true))
    {
      const PresetValue* path = FindPresetValue(values, lut_name);
      if (!path)
      {
        Error::SetStringFmt(error, "Texture '{}' has no path.", lut_name);
        return false;
      }

      const PresetValue* wrap = FindPresetValue(values, fmt::format("{}_wrap_mode", lut_name));
      m_luts.push_back(Lut{
        std::string(lut_name), ResolvePath(path->base_dir, path->value),
        MakeSamplerConfig(GetPresetBool(values, fmt::format("{}_linear", lut_name), false),
                          wrap ? ParseWrapMode(wrap->value) : GPUSampler::AddressMode::ClampToBorder),
        nullptr});
    }
  }

  m_passes.resize(pass_count);
  for (u32 i = 0; i < pass_count; i++)
  {
    Pass& pass = m_passes[i];
    const PresetValue* shader = FindPresetValue(values, fmt::format("shader{}", i));
    if (!shader)
    {
      Error::SetStringFmt(error, "Pass {} has no shader.", i);
      return false;
    }
    pass.shader_path = ResolvePath(shader->base_dir, shader->value);

    if (const PresetValue* alias = FindPresetValue(values, fmt::format("alias{}", i)))
      pass.alias = alias->value;

    const PresetValue* wrap = FindPresetValue(values, fmt::format("wrap_mode{}", i));
    pass.sampler.key = MakeSamplerConfig(GetPresetBool(values, fmt::format("filter_linear{}", i), false),
                                         wrap ? ParseWrapMode(wrap->value) : GPUSampler::AddressMode::ClampToBorder)
                         .key;

    const auto parse_scale_type = [](const PresetValue* value) -> std::optional<ScaleType> {
      if (!value)
        return std::nullopt;
      if (value->value == "viewport")
        return ScaleType::Viewport;
      if (value->value == "absolute")
        return ScaleType::Absolute;
      return ScaleType::Source;
    };
    const std::optional<ScaleType> scale_type = parse_scale_type(FindPresetValue(values, fmt::format("scale_type{}", i)));
    std::optional<ScaleType> scale_type_x = parse_scale_type(FindPresetValue(values, fmt::format("scale_type_x{}", i)));
    std::optional<ScaleType> scale_type_y = parse_scale_type(FindPresetValue(values, fmt::format("scale_type_y{}", i)));
    if (!scale_type_x.has_value())
      scale_type_x = scale_type;
    if (!scale_type_y.has_value())
      scale_type_y = scale_type;
    const ScaleType unset_type = (i == pass_count - 1) ? ScaleType::Viewport : ScaleType::Source;
    pass.scale_type_x = scale_type_x.value_or(unset_type);
    pass.scale_type_y = scale_type_y.value_or(unset_type);

    const auto parse_scale = [&values, i](std::string_view key, float default_value) {
      const PresetValue* value = FindPresetValue(values, fmt::format("{}{}", key, i));
      return value ? StringUtil::FromChars<float>(value->value).value_or(default_value) : default_value;
    };
    const float scale = parse_scale("scale", 1.0f);
    pass.scale_x = scale_type_x.has_value() ? parse_scale("scale_x", scale) : 1.0f;
    pass.scale_y = scale_type_y.has_value() ? parse_scale("scale_y", scale) : 1.0f;

    if (const PresetValue* mod = FindPresetValue(values, fmt::format("frame_count_mod{}", i)))
      pass.frame_count_mod = StringUtil::FromChars<u32>(mod->value).value_or(0);

    if (GetPresetBool(values, fmt::format("float_framebuffer{}", i), false))
      pass.format = GPUTexture::Format::RGBA16F;
  }

  std::vector<std::string> sources(pass_count);
  for (u32 i = 0; i < pass_count; i++)
  {
    if (!LoadPassSource(m_passes[i], &sources[i], error))
    {
      Error::AddPrefixFmt(error, "Pass {} ({}): ", i, Path::GetFileName(m_passes[i].shader_path));
      return false;
    }
  }
  for (u32 i = 0; i < pass_count; i++)
  {
    if (!ProcessPassSource(m_passes[i], i, std::move(sources[i]), error))
    {
      Error::AddPrefixFmt(error, "Pass {} ({}): ", i, Path::GetFileName(m_passes[i].shader_path));
      return false;
    }
  }

  if (const PresetValue* parameters = FindPresetValue(values, "parameters"))
  {
    for (const std::string_view parameter : StringUtil::SplitString(parameters->value, ';', true))
    {
      const std::optional<u32> index = FindOption(parameter);
      const PresetValue* value = FindPresetValue(values, parameter);
      if (!index.has_value() || !value)
        continue;

      ShaderOption& option = m_options[index.value()];
      option.default_value[0].float_value =
        StringUtil::FromChars<float>(value->value).value_or(option.default_value[0].float_value);
      option.value = option.default_value;
    }
  }

  return true;
}

bool PostProcessing::SlangShader::LoadPassSource(Pass& pass, std::string* declarations, Error* error)
{
  std::string source;
  if (!ReadShaderSource(pass.shader_path, &source, 0, error))
    return false;

  std::string vertex = "", fragment = "";
  std::string* current_stage = nullptr;
  for (std::string_view line : StringUtil::SplitString(source, '\n', false))
  {
    const std::string_view stripped = StringUtil::StripWhitespace(line);
    if (stripped.starts_with("#pragma"))
    {
      const std::vector<std::string_view> words = StringUtil::SplitString(stripped, ' ', true);
      const std::string_view directive = (words.size() > 1) ? words[1] : std::string_view();
      if (directive == "stage" && words.size() > 2)
      {
        current_stage = (words[2] == "vertex") ? &vertex : &fragment;
        vertex.push_back('\n');
        fragment.push_back('\n');
        continue;
      }
      else if (directive == "name" && words.size() > 2)
      {
        if (pass.alias.empty())
          pass.alias = std::string(words[2]);
      }
      else if (directive == "format" && words.size() > 2)
      {
        if (pass.format == GPUTexture::Format::RGBA8)
          pass.format = ParseFormatPragma(words[2]).value_or(pass.format);
      }
      else if (directive == "parameter" && words.size() > 2)
      {
        const size_t desc_start = stripped.find('"');
        const size_t desc_end = (desc_start != std::string_view::npos) ? stripped.find('"', desc_start + 1) :
                                                                          std::string_view::npos;
        if (desc_end != std::string_view::npos && !FindOption(words[2]).has_value())
        {
          const std::vector<std::string_view> numbers =
            StringUtil::SplitString(stripped.substr(desc_end + 1), ' ', true);
          const auto number = [&numbers](size_t i, float default_value) {
            return (i < numbers.size()) ? StringUtil::FromChars<float>(numbers[i]).value_or(default_value) :
                                          default_value;
          };

          ShaderOption option = {};
          option.name = std::string(words[2]);
          option.ui_name = std::string(stripped.substr(desc_start + 1, desc_end - desc_start - 1));
          option.type = ShaderOption::Type::Float;
          option.vector_size = 1;
          option.buffer_size = sizeof(float);
          option.default_value = ShaderOption::MakeFloatVector(number(0, 0.0f));
          option.min_value = ShaderOption::MakeFloatVector(number(1, 0.0f));
          option.max_value = ShaderOption::MakeFloatVector(number(2, 1.0f));
          option.step_value = ShaderOption::MakeFloatVector(number(3, 0.01f));
          option.value = option.default_value;
          m_options.push_back(std::move(option));
        }
      }

      vertex.push_back('\n');
      fragment.push_back('\n');
      continue;
    }

    if (current_stage)
    {
      current_stage->append(line);
      current_stage->push_back('\n');
      (current_stage == &vertex ? fragment : vertex).push_back('\n');
    }
    else
    {
      vertex.append(line);
      vertex.push_back('\n');
      fragment.append(line);
      fragment.push_back('\n');
    }
  }

  pass.vertex_source = StripComments(vertex);
  pass.fragment_source = StripComments(fragment);
  *declarations = StripComments(source);
  return true;
}

bool PostProcessing::SlangShader::ProcessPassSource(Pass& pass, u32 pass_index, std::string source, Error* error)
{
  const Declarations all = FindDeclarations(source);

  std::vector<UniformMember> members;
  u32 offset = 0;
  bool any_instance = false, any_anonymous = false;
  std::vector<std::string> instances;
  for (const UniformBlock& block : all.blocks)
  {
    if (block.instance.empty())
      any_anonymous = true;
    else if (std::find(instances.begin(), instances.end(), block.instance) == instances.end())
      instances.push_back(block.instance), any_instance = true;

    for (std::string_view decl : StringUtil::SplitString(block.body, ';', true))
    {
      decl = StringUtil::StripWhitespace(decl);
      const size_t type_end = decl.find_first_of(" \t\r\n");
      if (decl.empty() || type_end == std::string_view::npos)
        continue;

      const std::string_view type = decl.substr(0, type_end);
      const std::optional<Std140Type> info = GetStd140Type(type);
      if (!info.has_value())
      {
        Error::SetStringFmt(error, "Unsupported uniform type '{}'.", type);
        return false;
      }

      for (std::string_view name : StringUtil::SplitString(decl.substr(type_end), ',', true))
      {
        name = StringUtil::StripWhitespace(name);
        if (name.find('[') != std::string_view::npos)
        {
          Error::SetStringFmt(error, "Uniform arrays ('{}') are not supported.", name);
          return false;
        }
        if (std::any_of(members.begin(), members.end(), [&name](const UniformMember& m) { return m.name == name; }))
          continue;

        offset = (offset + info->align - 1) & ~(info->align - 1);
        members.push_back(UniformMember{std::string(type), std::string(name), offset, info->components,
                                        info->is_integer});
        offset += info->size;
      }
    }
  }
  if (any_instance && any_anonymous)
  {
    Error::SetStringView(error, "Mixing named and unnamed uniform blocks is not supported.");
    return false;
  }
  pass.uniforms_size = (offset + 15) & ~15u;

  std::vector<std::string> sampler_names;
  for (const SamplerDecl& sampler : all.samplers)
  {
    if (std::find(sampler_names.begin(), sampler_names.end(), sampler.name) == sampler_names.end())
      sampler_names.push_back(sampler.name);
  }
  if (sampler_names.size() > GPUDevice::MAX_TEXTURE_SAMPLERS)
  {
    Error::SetStringFmt(error, "Uses {} textures, at most {} are supported.", sampler_names.size(),
                        GPUDevice::MAX_TEXTURE_SAMPLERS);
    return false;
  }

  for (u32 slot = 0; slot < sampler_names.size(); slot++)
  {
    const std::optional<TextureRef> texture = LookupTexture(sampler_names[slot], pass_index);
    if (!texture.has_value())
    {
      Error::SetStringFmt(error, "Unknown texture '{}'.", sampler_names[slot]);
      return false;
    }

    NoteTextureUse(texture.value());
    pass.samplers.push_back(SamplerBinding{texture.value(), slot});
  }

  for (const UniformMember& member : members)
  {
    Uniform uniform = {};
    uniform.offset = member.offset;
    uniform.components = member.components;
    uniform.is_integer = member.is_integer;
    const std::string_view name = member.name;
    if (name == "MVP")
    {
      uniform.kind = UniformKind::MVP;
    }
    else if (name == "OutputSize")
    {
      uniform.kind = UniformKind::OutputSize;
    }
    else if (name == "FinalViewportSize")
    {
      uniform.kind = UniformKind::FinalViewportSize;
    }
    else if (name == "FrameCount")
    {
      uniform.kind = UniformKind::FrameCount;
    }
    else if (name == "FrameDirection")
    {
      uniform.kind = UniformKind::FrameDirection;
    }
    else if (const std::optional<u32> option = FindOption(name); option.has_value())
    {
      uniform.kind = UniformKind::Parameter;
      uniform.option_index = option.value();
    }
    else
    {
      std::string texture_name(name);
      for (const std::string_view prefix : {"OriginalHistorySize", "PassOutputSize", "PassFeedbackSize"})
      {
        if (name.starts_with(prefix))
        {
          texture_name = fmt::format("{}{}", prefix.substr(0, prefix.size() - 4), name.substr(prefix.size()));
          break;
        }
      }
      if (texture_name == name && name.ends_with("Size"))
        texture_name.resize(texture_name.size() - 4);

      const std::optional<TextureRef> texture = LookupTexture(texture_name, pass_index);
      if (!texture.has_value())
      {
        DEV_LOG("{}: pass {} has unknown uniform '{}', left at zero.", m_name, pass_index, name);
        continue;
      }

      NoteTextureUse(texture.value());
      uniform.kind = UniformKind::TextureSize;
      uniform.texture = texture.value();
    }

    pass.uniforms.push_back(uniform);
  }

  std::string header;
  if (!members.empty())
  {
    header = "layout(std140, set = 0, binding = 0) uniform RHSlangUniforms\n{\n";
    for (const UniformMember& member : members)
      header += fmt::format("  {} {};\n", member.type, member.name);
    header += any_instance ? fmt::format("}} {};\n", MERGED_BLOCK_INSTANCE) : std::string("};\n");
    for (const std::string& instance : instances)
      header += fmt::format("#define {} {}\n", instance, MERGED_BLOCK_INSTANCE);
  }

  const auto rewrite_stage = [&](std::string* text, bool vertex) {
    const Declarations decls = FindDeclarations(*text);
    std::vector<std::pair<std::pair<size_t, size_t>, std::string>> edits;
    for (const UniformBlock& block : decls.blocks)
      edits.emplace_back(std::make_pair(block.start, block.end), std::string());
    for (const SamplerDecl& sampler : decls.samplers)
    {
      const u32 slot = static_cast<u32>(
        std::find(sampler_names.begin(), sampler_names.end(), sampler.name) - sampler_names.begin());
      edits.emplace_back(std::make_pair(sampler.start, sampler.end),
                         fmt::format("layout(set = 1, binding = {}) uniform sampler2D {};", slot, sampler.name));
    }

    std::string vertex_main;
    if (vertex)
    {
      vertex_main = "\n#undef main\nvoid main()\n{\n  vec2 rh_slang_uv = vec2(float((gl_VertexIndex << 1) & 2), "
                    "float(gl_VertexIndex & 2));\n";
      for (const InputDecl& input : decls.inputs)
      {
        edits.emplace_back(std::make_pair(input.start, input.end), fmt::format("{} {};", input.type, input.name));
        if (input.location == 0)
          vertex_main += fmt::format("  {} = vec4(rh_slang_uv, 0.0, 1.0);\n", input.name);
        else if (input.location == 1)
          vertex_main += fmt::format("  {} = rh_slang_uv;\n", input.name);
      }
      vertex_main += "  rh_slang_main();\n}\n";
    }

    ReplaceRanges(text, std::move(edits));
    text->insert(FindHeaderEnd(*text), header + (vertex ? "#define main rh_slang_main\n" : ""));
    text->append(vertex_main);
  };

  rewrite_stage(&pass.vertex_source, true);
  rewrite_stage(&pass.fragment_source, false);
  return true;
}

std::optional<PostProcessing::SlangShader::TextureRef> PostProcessing::SlangShader::LookupTexture(std::string_view name,
                                                                                                  u32 pass_index) const
{
  const auto numbered = [&name](std::string_view prefix) -> std::optional<u32> {
    if (!name.starts_with(prefix) || name.size() == prefix.size())
      return std::nullopt;
    return StringUtil::FromChars<u32>(name.substr(prefix.size()));
  };

  if (name == "Original")
    return TextureRef{TextureKind::Original, 0};
  if (name == "Source")
    return TextureRef{TextureKind::Source, 0};
  if (const std::optional<u32> index = numbered("OriginalHistory"); index.has_value())
    return TextureRef{(index.value() == 0) ? TextureKind::Original : TextureKind::OriginalHistory, index.value()};
  if (const std::optional<u32> index = numbered("PassOutput"); index.has_value() && index.value() < pass_index)
    return TextureRef{TextureKind::PassOutput, index.value()};
  if (const std::optional<u32> index = numbered("PassFeedback"); index.has_value() && index.value() < m_passes.size())
    return TextureRef{TextureKind::PassFeedback, index.value()};

  for (u32 i = 0; i < m_passes.size(); i++)
  {
    const std::string& alias = m_passes[i].alias;
    if (alias.empty())
      continue;
    if (i < pass_index && name == alias)
      return TextureRef{TextureKind::PassOutput, i};
    if (name.starts_with(alias) && name.substr(alias.size()) == "Feedback")
      return TextureRef{TextureKind::PassFeedback, i};
  }

  for (u32 i = 0; i < m_luts.size(); i++)
  {
    if (name == m_luts[i].name)
      return TextureRef{TextureKind::Lut, i};
  }

  return std::nullopt;
}

void PostProcessing::SlangShader::NoteTextureUse(const TextureRef& ref)
{
  if (ref.kind == TextureKind::PassFeedback)
    m_passes[ref.index].feedback_used = true;
  else if (ref.kind == TextureKind::OriginalHistory)
    m_history_size = std::max(m_history_size, ref.index);
}

std::optional<u32> PostProcessing::SlangShader::FindOption(std::string_view name) const
{
  for (u32 i = 0; i < m_options.size(); i++)
  {
    if (m_options[i].name == name)
      return i;
  }
  return std::nullopt;
}

bool PostProcessing::SlangShader::ResizeOutput(GPUTexture::Format format, u32 width, u32 height)
{
  return true;
}

bool PostProcessing::SlangShader::CreateCropPipeline(Error* error)
{
  static constexpr const char* vertex_source = R"(#version 450
layout(std140, set = 0, binding = 0) uniform CropUniforms
{
  vec4 src_rect;
  vec4 clip_scale;
};
layout(location = 0) out vec2 v_uv;
void main()
{
  vec2 uv = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
  v_uv = mix(src_rect.xy, src_rect.zw, uv);
  gl_Position = vec4((uv * 2.0 - 1.0) * clip_scale.xy, 0.0, 1.0);
}
)";
  static constexpr const char* fragment_source = R"(#version 450
layout(set = 1, binding = 0) uniform sampler2D samp0;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_col0;
void main()
{
  o_col0 = vec4(texture(samp0, v_uv).rgb, 1.0);
}
)";

  std::unique_ptr<GPUShader> vs =
    g_gpu_device->CreateShader(GPUShaderStage::Vertex, GPUShaderLanguage::GLSLVK, vertex_source, error);
  std::unique_ptr<GPUShader> fs =
    g_gpu_device->CreateShader(GPUShaderStage::Fragment, GPUShaderLanguage::GLSLVK, fragment_source, error);
  if (!vs || !fs)
    return false;

  GPUPipeline::GraphicsConfig plconfig = {};
  plconfig.layout = GPUPipeline::Layout::MultiTextureAndUBO;
  plconfig.primitive = GPUPipeline::Primitive::Triangles;
  plconfig.rasterization = GPUPipeline::RasterizationState::GetNoCullState();
  plconfig.depth = GPUPipeline::DepthState::GetNoTestsState();
  plconfig.blend = GPUPipeline::BlendState::GetNoBlendingState();
  plconfig.vertex_shader = vs.get();
  plconfig.fragment_shader = fs.get();
  plconfig.SetTargetFormats(GPUTexture::Format::RGBA8);
  plconfig.samples = 1;
  m_crop_pipeline = g_gpu_device->CreatePipeline(plconfig, error);
  return static_cast<bool>(m_crop_pipeline);
}

bool PostProcessing::SlangShader::CompilePipeline(GPUTexture::Format format, u32 width, u32 height,
                                                  ProgressCallback* progress)
{
  m_valid = false;

  Error error;
  if (!CreateCropPipeline(&error))
  {
    ERROR_LOG("{}: failed to create the input pipeline: {}", m_name, error.GetDescription());
    return false;
  }

  for (Lut& lut : m_luts)
  {
    RGBA8Image image;
    if (!image.LoadFromFile(lut.path.c_str()))
    {
      ERROR_LOG("{}: failed to load texture '{}'.", m_name, Path::GetFileName(lut.path));
      return false;
    }

    lut.texture = g_gpu_device->FetchTexture(image.GetWidth(), image.GetHeight(), 1, 1, 1, GPUTexture::Type::Texture,
                                             GPUTexture::Format::RGBA8, image.GetPixels(), image.GetPitch());
    if (!lut.texture)
      return false;
  }

  progress->PushState();
  progress->SetProgressRange(static_cast<u32>(m_passes.size()));
  for (u32 i = 0; i < m_passes.size(); i++)
  {
    Pass& pass = m_passes[i];
    progress->SetProgressValue(i);

    std::unique_ptr<GPUShader> vs =
      g_gpu_device->CreateShader(GPUShaderStage::Vertex, GPUShaderLanguage::GLSLVK, pass.vertex_source, &error);
    std::unique_ptr<GPUShader> fs =
      vs ? g_gpu_device->CreateShader(GPUShaderStage::Fragment, GPUShaderLanguage::GLSLVK, pass.fragment_source,
                                      &error) :
           nullptr;
    if (!vs || !fs)
    {
      ERROR_LOG("{}: pass {} ({}) failed to compile: {}", m_name, i, Path::GetFileName(pass.shader_path),
                error.GetDescription());
      progress->PopState();
      return false;
    }

    GPUPipeline::GraphicsConfig plconfig = {};
    plconfig.layout = GPUPipeline::Layout::MultiTextureAndUBO;
    plconfig.primitive = GPUPipeline::Primitive::Triangles;
    plconfig.rasterization = GPUPipeline::RasterizationState::GetNoCullState();
    plconfig.depth = GPUPipeline::DepthState::GetNoTestsState();
    plconfig.blend = GPUPipeline::BlendState::GetNoBlendingState();
    plconfig.vertex_shader = vs.get();
    plconfig.fragment_shader = fs.get();
    plconfig.SetTargetFormats((i == m_passes.size() - 1) ? format : pass.format);
    plconfig.samples = 1;
    pass.pipeline = g_gpu_device->CreatePipeline(plconfig, &error);
    if (!pass.pipeline)
    {
      ERROR_LOG("{}: pass {} pipeline failed: {}", m_name, i, error.GetDescription());
      progress->PopState();
      return false;
    }
  }
  progress->PopState();

  m_valid = true;
  return true;
}

bool PostProcessing::SlangShader::EnsureTexture(std::unique_ptr<GPUTexture>& texture, u32 width, u32 height,
                                                GPUTexture::Format format)
{
  if (texture && texture->GetWidth() == width && texture->GetHeight() == height && texture->GetFormat() == format)
    return true;

  g_gpu_device->RecycleTexture(std::move(texture));
  texture = g_gpu_device->FetchTexture(width, height, 1, 1, 1, GPUTexture::Type::RenderTarget, format);
  if (!texture)
  {
    ERROR_LOG("{}: failed to create {}x{} texture.", m_name, width, height);
    return false;
  }

  g_gpu_device->ClearRenderTarget(texture.get(), 0);
  return true;
}

bool PostProcessing::SlangShader::CropOriginal(GPUTexture* input_color, GSVector4i src_rect, u32 target_width,
                                               u32 target_height, s32 orig_width, s32 orig_height)
{
  if (!EnsureTexture(m_original, static_cast<u32>(std::max(orig_width, 1)), static_cast<u32>(std::max(orig_height, 1)),
                     GPUTexture::Format::RGBA8))
  {
    return false;
  }

  if (g_gpu_device->UsesLowerLeftOrigin())
    src_rect = GPUDevice::FlipToLowerLeft(src_rect, static_cast<s32>(target_height));

  const RenderAPI api = g_gpu_device->GetRenderAPI();
  const float y_sign = (api == RenderAPI::D3D11 || api == RenderAPI::D3D12 || api == RenderAPI::Metal) ? -1.0f : 1.0f;
  const float uniforms[8] = {static_cast<float>(src_rect.left) / static_cast<float>(target_width),
                             static_cast<float>(src_rect.top) / static_cast<float>(target_height),
                             static_cast<float>(src_rect.right) / static_cast<float>(target_width),
                             static_cast<float>(src_rect.bottom) / static_cast<float>(target_height),
                             1.0f,
                             y_sign,
                             0.0f,
                             0.0f};

  GPUTexture* const original = m_original.get();
  g_gpu_device->SetRenderTargets(&original, 1, nullptr);
  g_gpu_device->SetPipeline(m_crop_pipeline.get());
  g_gpu_device->SetTextureSampler(0, input_color, g_gpu_device->GetNearestSampler());
  g_gpu_device->SetViewportAndScissor(GSVector4i(0, 0, original->GetWidth(), original->GetHeight()));
  std::memcpy(g_gpu_device->MapUniformBuffer(sizeof(uniforms)), uniforms, sizeof(uniforms));
  g_gpu_device->UnmapUniformBuffer(sizeof(uniforms));
  g_gpu_device->Draw(3, 0);
  original->MakeReadyForSampling();
  return true;
}

GPUTexture* PostProcessing::SlangShader::GetTexture(const TextureRef& ref, u32 pass_index) const
{
  GPUTexture* texture = nullptr;
  switch (ref.kind)
  {
    case TextureKind::Original:
      texture = m_original.get();
      break;
    case TextureKind::Source:
      texture = (pass_index == 0) ? m_original.get() : m_passes[pass_index - 1].output.get();
      break;
    case TextureKind::OriginalHistory:
      texture = (ref.index - 1 < m_history.size()) ? m_history[ref.index - 1].get() : m_original.get();
      break;
    case TextureKind::PassOutput:
      texture = m_passes[ref.index].output.get();
      break;
    case TextureKind::PassFeedback:
      texture = m_passes[ref.index].feedback.get();
      break;
    case TextureKind::Lut:
      texture = m_luts[ref.index].texture.get();
      break;
  }

  return texture ? texture : PostProcessing::GetDummyTexture();
}

void PostProcessing::SlangShader::FillUniforms(const Pass& pass, u32 pass_index, u8* buffer, u32 output_width,
                                               u32 output_height, const GSVector4i& final_rect) const
{
  std::memset(buffer, 0, pass.uniforms_size);

  const auto write = [buffer](const Uniform& uniform, std::initializer_list<float> values) {
    u32 i = 0;
    for (const float value : values)
    {
      if (i == uniform.components)
        break;

      u8* const dst = buffer + uniform.offset + i * sizeof(u32);
      if (uniform.is_integer)
      {
        const s32 int_value = static_cast<s32>(value);
        std::memcpy(dst, &int_value, sizeof(int_value));
      }
      else
      {
        std::memcpy(dst, &value, sizeof(value));
      }
      i++;
    }
  };
  const auto size_values = [](float width, float height) {
    return std::array<float, 4>{width, height, 1.0f / width, 1.0f / height};
  };

  for (const Uniform& uniform : pass.uniforms)
  {
    switch (uniform.kind)
    {
      case UniformKind::MVP:
      {
        const RenderAPI api = g_gpu_device->GetRenderAPI();
        const float s = (api == RenderAPI::D3D11 || api == RenderAPI::D3D12 || api == RenderAPI::Metal) ? -1.0f : 1.0f;
        write(uniform, {2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f * s, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -1.0f, -s, 0.0f,
                        1.0f});
      }
      break;

      case UniformKind::OutputSize:
      {
        const auto v = size_values(static_cast<float>(output_width), static_cast<float>(output_height));
        write(uniform, {v[0], v[1], v[2], v[3]});
      }
      break;

      case UniformKind::FinalViewportSize:
      {
        const auto v = size_values(static_cast<float>(final_rect.width()), static_cast<float>(final_rect.height()));
        write(uniform, {v[0], v[1], v[2], v[3]});
      }
      break;

      case UniformKind::FrameCount:
        write(uniform,
              {static_cast<float>(pass.frame_count_mod ? (m_frame_count % pass.frame_count_mod) : m_frame_count)});
        break;

      case UniformKind::FrameDirection:
        write(uniform, {1.0f});
        break;

      case UniformKind::TextureSize:
      {
        const GPUTexture* texture = GetTexture(uniform.texture, pass_index);
        const auto v = size_values(static_cast<float>(texture->GetWidth()), static_cast<float>(texture->GetHeight()));
        write(uniform, {v[0], v[1], v[2], v[3]});
      }
      break;

      case UniformKind::Parameter:
        write(uniform, {m_options[uniform.option_index].value[0].float_value});
        break;
    }
  }
}

bool PostProcessing::SlangShader::Apply(GPUTexture* input_color, GPUTexture* input_depth, GPUTexture* final_target,
                                        GSVector4i final_rect, s32 orig_width, s32 orig_height, s32 native_width,
                                        s32 native_height, u32 target_width, u32 target_height)
{
  GL_SCOPE_FMT("Slang Shader {}", m_name);

  m_frame_count++;
  if (!CropOriginal(input_color, final_rect, target_width, target_height, orig_width, orig_height))
    return false;

  for (Pass& pass : m_passes)
  {
    if (pass.feedback_used)
      std::swap(pass.output, pass.feedback);
  }

  const GSVector4i final_viewport = g_gpu_device->UsesLowerLeftOrigin() ?
                                      GPUDevice::FlipToLowerLeft(final_rect, static_cast<s32>(target_height)) :
                                      final_rect;
  for (u32 i = 0; i < m_passes.size(); i++)
  {
    Pass& pass = m_passes[i];
    const bool is_final = (i == m_passes.size() - 1);
    const GPUTexture* const source = (i == 0) ? m_original.get() : m_passes[i - 1].output.get();
    const auto scaled = [&final_rect](ScaleType type, float scale, u32 source_size, s32 viewport_size) {
      const float base = (type == ScaleType::Source) ?
                           static_cast<float>(source_size) :
                           ((type == ScaleType::Viewport) ? static_cast<float>(viewport_size) : 1.0f);
      return std::max(static_cast<u32>(base * scale + 0.5f), 1u);
    };

    u32 output_width, output_height;
    GSVector4i viewport;
    if (is_final)
    {
      output_width = static_cast<u32>(final_rect.width());
      output_height = static_cast<u32>(final_rect.height());
      viewport = final_viewport;
    }
    else
    {
      output_width = scaled(pass.scale_type_x, pass.scale_x, source->GetWidth(), final_rect.width());
      output_height = scaled(pass.scale_type_y, pass.scale_y, source->GetHeight(), final_rect.height());
      if (!EnsureTexture(pass.output, output_width, output_height, pass.format))
        return false;
      viewport = GSVector4i(0, 0, static_cast<s32>(output_width), static_cast<s32>(output_height));
    }

    for (const SamplerBinding& binding : pass.samplers)
      GetTexture(binding.texture, i)->MakeReadyForSampling();

    if (!is_final)
    {
      GPUTexture* const output = pass.output.get();
      g_gpu_device->SetRenderTargets(&output, 1, nullptr);
    }
    else if (final_target)
    {
      g_gpu_device->SetRenderTargets(&final_target, 1, nullptr);
      g_gpu_device->ClearRenderTarget(final_target, 0);
    }
    else if (!g_gpu_device->BeginPresent(false))
    {
      return false;
    }

    g_gpu_device->SetPipeline(pass.pipeline.get());
    for (const SamplerBinding& binding : pass.samplers)
    {
      const GPUSampler::Config& config =
        (binding.texture.kind == TextureKind::Lut) ? m_luts[binding.texture.index].sampler : pass.sampler;
      g_gpu_device->SetTextureSampler(binding.slot, GetTexture(binding.texture, i), PostProcessing::GetSampler(config));
    }
    g_gpu_device->SetViewportAndScissor(viewport);

    if (pass.uniforms_size > 0)
    {
      u8* const uniforms = static_cast<u8*>(g_gpu_device->MapUniformBuffer(pass.uniforms_size));
      FillUniforms(pass, i, uniforms, output_width, output_height, final_rect);
      g_gpu_device->UnmapUniformBuffer(pass.uniforms_size);
    }

    g_gpu_device->Draw(3, 0);
    if (!is_final)
      pass.output->MakeReadyForSampling();
  }

  if (m_history_size > 0)
  {
    std::unique_ptr<GPUTexture> oldest;
    if (m_history.size() >= m_history_size)
    {
      oldest = std::move(m_history.back());
      m_history.pop_back();
    }
    if (!EnsureTexture(oldest, m_original->GetWidth(), m_original->GetHeight(), m_original->GetFormat()))
      return false;

    g_gpu_device->CopyTextureRegion(oldest.get(), 0, 0, 0, 0, m_original.get(), 0, 0, 0, 0, m_original->GetWidth(),
                                    m_original->GetHeight());
    m_history.push_front(std::move(oldest));
  }

  return true;
}
