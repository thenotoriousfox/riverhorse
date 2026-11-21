// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "libretro_video_vulkan.h"

#include "core/gpu.h"
#include "core/host.h"
#include "core/system.h"

#include "util/vulkan_device.h"
#include "util/vulkan_texture.h"

#include "common/error.h"
#include "common/log.h"


#include <libretro_vulkan.h>

#include <array>
#include <memory>

Log_SetChannel(LibretroVulkan);

namespace LibretroVulkan {
static bool CreateDevice(retro_vulkan_context* context, VkInstance instance, VkPhysicalDevice gpu,
                         VkSurfaceKHR surface, PFN_vkGetInstanceProcAddr get_instance_proc_addr,
                         const char** required_device_extensions, unsigned num_required_device_extensions,
                         const char** required_device_layers, unsigned num_required_device_layers,
                         const VkPhysicalDeviceFeatures* required_features);
static const VkApplicationInfo* GetApplicationInfo();

static constexpr u32 MAX_SYNC_INDICES = 32;

static const retro_hw_render_interface_vulkan* s_interface = nullptr;
static std::array<std::unique_ptr<GPUTexture>, MAX_SYNC_INDICES> s_frame_textures;
static std::array<retro_vulkan_image, MAX_SYNC_INDICES> s_frame_images;
} // namespace LibretroVulkan

const VkApplicationInfo* LibretroVulkan::GetApplicationInfo()
{
  static constexpr VkApplicationInfo info = {VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "Riverhorse", 0,
                                             "Riverhorse", 0, VK_API_VERSION_1_1};
  return &info;
}

bool LibretroVulkan::Setup(retro_environment_t environment, retro_hw_context_reset_t context_reset,
                           retro_hw_context_reset_t context_destroy)
{
  retro_hw_context_type preferred = RETRO_HW_CONTEXT_NONE;
  if (!environment(RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER, &preferred) || preferred != RETRO_HW_CONTEXT_VULKAN)
    return false;

  static retro_hw_render_callback hw_render;
  hw_render = {};
  hw_render.context_type = RETRO_HW_CONTEXT_VULKAN;
  hw_render.version_major = VK_API_VERSION_1_1;
  hw_render.context_reset = context_reset;
  hw_render.context_destroy = context_destroy;
  if (!environment(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
    return false;

  static constexpr retro_hw_render_context_negotiation_interface_vulkan negotiation = {
    RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN,
    1,
    &GetApplicationInfo,
    &CreateDevice,
    nullptr,
  };
  if (!environment(RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE,
                   const_cast<retro_hw_render_context_negotiation_interface_vulkan*>(&negotiation)))
  {
    ERROR_LOG("The frontend cannot let the core create its Vulkan device.");
    return false;
  }

  return true;
}

bool LibretroVulkan::CreateDevice(retro_vulkan_context* context, VkInstance instance, VkPhysicalDevice gpu,
                                  VkSurfaceKHR surface, PFN_vkGetInstanceProcAddr get_instance_proc_addr,
                                  const char** required_device_extensions, unsigned num_required_device_extensions,
                                  const char** required_device_layers, unsigned num_required_device_layers,
                                  const VkPhysicalDeviceFeatures* required_features)
{
  VulkanDevice::HostContext host_context = {};
  host_context.instance = instance;
  host_context.physical_device = gpu;
  host_context.surface = surface;
  host_context.device_extensions.assign(required_device_extensions,
                                        required_device_extensions + num_required_device_extensions);
  if (required_features)
    host_context.device_features = *required_features;
  host_context.lock_queue = []() {
    if (s_interface)
      s_interface->lock_queue(s_interface->handle);
  };
  host_context.unlock_queue = []() {
    if (s_interface)
      s_interface->unlock_queue(s_interface->handle);
  };
  VulkanDevice::SetHostContext(std::move(host_context));

  Error error;
  if (!Host::CreateGPUDevice(RenderAPI::Vulkan, &error))
  {
    ERROR_LOG("Failed to create the Vulkan device: {}", error.GetDescription());
    VulkanDevice::SetHostContext(std::nullopt);
    return false;
  }

  const VulkanDevice& device = VulkanDevice::GetInstance();
  context->gpu = device.GetVulkanPhysicalDevice();
  context->device = device.GetVulkanDevice();
  context->queue = device.GetGraphicsQueue();
  context->queue_family_index = device.GetGraphicsQueueFamilyIndex();
  context->presentation_queue = device.GetPresentQueue() ? device.GetPresentQueue() : device.GetGraphicsQueue();
  context->presentation_queue_family_index = device.GetPresentQueue() ? device.GetPresentQueueFamilyIndex() :
                                                                        device.GetGraphicsQueueFamilyIndex();
  return true;
}

void LibretroVulkan::Shutdown()
{
  VulkanDevice::SetHostContext(std::nullopt);
  s_interface = nullptr;
}

void LibretroVulkan::OnContextReset(retro_environment_t environment)
{
  const retro_hw_render_interface* hw_interface = nullptr;
  if (!environment(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, &hw_interface) || !hw_interface ||
      hw_interface->interface_type != RETRO_HW_RENDER_INTERFACE_VULKAN)
  {
    ERROR_LOG("The frontend did not provide a Vulkan render interface.");
    return;
  }

  s_interface = reinterpret_cast<const retro_hw_render_interface_vulkan*>(hw_interface);
}

void LibretroVulkan::DestroyFrameResources()
{
  for (std::unique_ptr<GPUTexture>& texture : s_frame_textures)
  {
    if (texture)
      g_gpu_device->RecycleTexture(std::move(texture));
  }
}

bool LibretroVulkan::RenderFrame(u32* width, u32* height)
{
  *width = g_gpu->GetDisplayTextureViewWidth();
  *height = g_gpu->GetDisplayTextureViewHeight();
  if (!s_interface || *width == 0 || *height == 0)
    return false;

  const u32 sync_index = s_interface->get_sync_index(s_interface->handle) % MAX_SYNC_INDICES;
  std::unique_ptr<GPUTexture>& texture = s_frame_textures[sync_index];
  if (!texture || texture->GetWidth() != *width || texture->GetHeight() != *height)
  {
    if (texture)
      g_gpu_device->RecycleTexture(std::move(texture));
    texture =
      g_gpu_device->FetchTexture(*width, *height, 1, 1, 1, GPUTexture::Type::RenderTarget, GPUTexture::Format::RGBA8);
    if (!texture)
    {
      ERROR_LOG("Failed to create a {}x{} frame texture.", *width, *height);
      return false;
    }
  }

  if (!g_gpu->RenderDisplayAtInternalResolution(texture.get(), true))
    return false;

  VulkanTexture* vk_texture = static_cast<VulkanTexture*>(texture.get());
  vk_texture->TransitionToLayout(VulkanTexture::Layout::ShaderReadOnly);
  VulkanDevice::GetInstance().SubmitCommandBuffer(false);

  retro_vulkan_image& image = s_frame_images[sync_index];
  image = {};
  image.image_view = vk_texture->GetView();
  image.image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  image.create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  image.create_info.image = vk_texture->GetImage();
  image.create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  image.create_info.format = vk_texture->GetVkFormat();
  image.create_info.components = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B,
                                  VK_COMPONENT_SWIZZLE_A};
  image.create_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  s_interface->set_image(s_interface->handle, &image, 0, nullptr, VK_QUEUE_FAMILY_IGNORED);
  return true;
}
