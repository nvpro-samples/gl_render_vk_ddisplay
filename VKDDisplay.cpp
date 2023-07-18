/*
 * Copyright (c) 2014-2023, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2014-2023, NVIDIA CORPORATION
 * SPDX-License-Identifier: Apache-2.0
 */


 /* Contact iesser@nvidia.com (Ingo Esser) for feedback */

#include "VKDDisplay.h"

#include <algorithm>
#include <format>
#include <iostream>


VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE


typedef VkResult(*PFN_vkAcquireWinrtDisplayNV)(VkPhysicalDevice physicalDevice, VkDisplayKHR display);
PFN_vkAcquireWinrtDisplayNV pfn_vkAcquireWinrtDisplayNV = nullptr;

// required instance extenstions
const std::vector<const char*> requiredInstanceExtensions = { 
  VK_KHR_SURFACE_EXTENSION_NAME, 
  VK_KHR_DISPLAY_EXTENSION_NAME,
  VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
  VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
  VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
  VK_EXT_DIRECT_MODE_DISPLAY_EXTENSION_NAME 
};

// required device extensions
const std::vector<const char*> requiredDeviceExtensions = {
  VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
  VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
  VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
  VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
  VK_NV_ACQUIRE_WINRT_DISPLAY_EXTENSION_NAME
};

VKDirectDisplay::VKDirectDisplay() {}

bool VKDirectDisplay::init()
{
  try
  {
    createInstance();
    pickGPU();
    createDisplaySurface();
    createLogicalDevice();
    createCommandPool();
    createSwapchain();
    createSyncObjects();
    createSyncs();
    createCommandBuffers();
    return true;
  }
  catch(std::exception const& e)
  {
    LOGE("VKDirectDisplay::init() failed: %s\n", e.what());
    return false;
  }
}

void VKDirectDisplay::shutdown()
{
  m_device->waitIdle();
}

GLuint VKDirectDisplay::getTexture()
{
  // GL: wait for VK image available
  glWaitSemaphoreEXT(m_syncData[m_frameIndex].m_availableGL, 0, nullptr, 0, nullptr, nullptr);

  return m_syncData[m_frameIndex].m_textureGL;
}

void VKDirectDisplay::submitTexture()
{
  // GL: signal rendering is done
  // VK: acquire image from swapchain
  // VK: blit texture to swapchain image (wait for GL finished, VK image acquired. signal VK blit done)
  // present (wait for VK blit done. signal VK image available)

  // limit frames in flight
  m_device->waitForFences(m_fences[m_frameIndex].get(), VK_TRUE, UINT64_MAX);
  m_device->resetFences({ m_fences[m_frameIndex].get() });

  // GL: signal to VK that rendering is done
  glSignalSemaphoreEXT(m_syncData[m_frameIndex].m_finishedGL, 0, nullptr, 0, nullptr, nullptr);

  // RFE: handle return values
  auto r = m_device->acquireNextImageKHR(m_swapchain.get(), std::numeric_limits<uint64_t>::max(),
                                         m_imageAcquiredSemaphores[m_frameIndex].get());
  assert(m_frameIndex == r.value);  // this should be guaranteed, decoupling would mean N*M prepared blit command buffers
  
  // wait for GL finished & VK imageAcquired
  // blit/copy current texture onto current swapchain image
  // signal VK blit finished
  std::vector<vk::Semaphore> blitWaitSemaphores{m_syncData[m_frameIndex].m_finished.get(),
                                                m_imageAcquiredSemaphores[m_frameIndex].get()};
  std::vector<vk::PipelineStageFlags> blitWaitStages{vk::PipelineStageFlagBits::eColorAttachmentOutput, 
                                                     vk::PipelineStageFlagBits::eColorAttachmentOutput};
  std::vector<vk::Semaphore> blitSignalSemaphores{m_blitFinishedSemaphores[m_frameIndex].get()};

  vk::SubmitInfo submitInfo{blitWaitSemaphores,
                            blitWaitStages, 
                            m_blitCommandBuffers[m_frameIndex],
                            blitSignalSemaphores };
  m_presentQueue.submit(submitInfo, m_fences[m_frameIndex].get());

  // wait for VK blit finished
  // present
  std::vector<vk::Semaphore> presentWaitSemaphores{m_blitFinishedSemaphores[m_frameIndex].get()};
  vk::PresentInfoKHR presentInfo{ presentWaitSemaphores,
                                 m_swapchain.get(),
                                 m_frameIndex };  
  // VK_KHR_display
  // present on Direct Display output
  auto const present_result = m_presentQueue.presentKHR(presentInfo);

  // signal to GL that the interop texture is available
  vk::SubmitInfo signalInfo{ {},{},{}, m_syncData[m_frameIndex].m_available.get() };
  m_presentQueue.submit(signalInfo);

  m_frameIndex = (m_frameIndex + 1) % m_swapchainImages.size();
}

void VKDirectDisplay::createInstance()
{
  vk::DynamicLoader         dl;
  PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr =
    dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
  VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

  // check for required instance extensions
  std::vector<vk::ExtensionProperties> availableInstanceExtensions = vk::enumerateInstanceExtensionProperties();
  {
    uint32_t major = VK_API_VERSION_MAJOR(VK_HEADER_VERSION_COMPLETE);
    uint32_t minor = VK_API_VERSION_MINOR(VK_HEADER_VERSION_COMPLETE);
    uint32_t patch = VK_API_VERSION_PATCH(VK_HEADER_VERSION_COMPLETE);
    LOGI(std::format("\n\nVK Header version: {}.{}.{}\n", major, minor, patch).c_str());
  }

  std::cout << "\nChecking Instance Extensions\n";

  for(const auto& required : requiredInstanceExtensions)
  {
    bool found = false;
    for(const auto& available : availableInstanceExtensions)
    {
      if(std::string(required) == available.extensionName)
      {
        found = true;
        std::cout << "OK: " << required << "\n";
        break;
      }
    }
    if(!found)
    {
      throw std::exception(("Required instance extension not found: " + std::string(required) + "\n").c_str());
    }
  }
  vk::InstanceCreateInfo createInfo{
      vk::InstanceCreateFlags(), nullptr, 0, nullptr,
      uint32_t(requiredInstanceExtensions.size()), requiredInstanceExtensions.data()};
  m_instance = vk::createInstanceUnique(createInfo);

  VULKAN_HPP_DEFAULT_DISPATCHER.init(m_instance.get());

  {
    uint32_t apiVersion;
    vkEnumerateInstanceVersion(&apiVersion);
    uint32_t major = VK_API_VERSION_MAJOR(apiVersion);
    uint32_t minor = VK_API_VERSION_MINOR(apiVersion);
    uint32_t patch = VK_API_VERSION_PATCH(apiVersion);
    LOGI(std::format("Instance version: {}.{}.{}", major, minor, patch).c_str());
  }

  pfn_vkAcquireWinrtDisplayNV = (PFN_vkAcquireWinrtDisplayNV)vkGetInstanceProcAddr(m_instance.get(), "vkAcquireWinrtDisplayNV");
}

bool VKDirectDisplay::checkDeviceExtensionSupport(vk::PhysicalDevice device)
{
  std::vector<vk::ExtensionProperties> availableDeviceExtensions = device.enumerateDeviceExtensionProperties();

  std::cout << "\nChecking Device Extensions\n";

  for(const auto& required : requiredDeviceExtensions)
  {
    bool found = false;
    for(const auto& available : availableDeviceExtensions)
    {
      if(std::string(required) == available.extensionName)
      {
        found = true;
        std::cout << "OK: " << required << "\n";
        break;
      }
    }
    if(!found)
    {
      std::cerr << "\x1B[31mNOT FOUND: " << required << "\033[0m\n";
      return false;
    }
  }
  return true;
}

void VKDirectDisplay::pickGPU()
{
  // pick a GPU that has the required device extensions and has a display device attached
  std::vector<vk::PhysicalDevice> devices = m_instance->enumeratePhysicalDevices();
  LOGI("\n\nFinding GPU with suitable display...\n\n");
  for(const auto& device : devices)
  {
    const auto props = device.getProperties();
    LOGI(std::format("\nName:        {}\n", props.deviceName).c_str());

    uint32_t major = VK_API_VERSION_MAJOR(props.apiVersion);
    uint32_t minor = VK_API_VERSION_MINOR(props.apiVersion);
    uint32_t patch = VK_API_VERSION_PATCH(props.apiVersion);
    LOGI(std::format("API version: {}.{}.{}\n", major, minor, patch).c_str());

    if(!device.getDisplayPropertiesKHR().empty() && checkDeviceExtensionSupport(device))
    {
      // VK_KHR_display
      // GPU with ddisplay found
      LOGI("Suitable device found\n");
      m_gpu = device;
      break;
    }
    LOGE("Device not suitable\n");
  }
  if(!m_gpu)
  {
    throw std::exception("Could not find a GPU with suitable display device!");
  }

}

void VKDirectDisplay::createDisplaySurface()
{
  // RFE: make display id & resolution cmd line controllable?

  // VK_KHR_display
  // create a display surface for ddisplay

  // pick first available display
  m_display.displayProperties = m_gpu.getDisplayPropertiesKHR()[0];
  m_display.displayKHR        = m_display.displayProperties.display;

  // acquire display
  m_gpu.acquireWinrtDisplayNV(m_display.displayKHR);

  // pick highest available resolution
  auto modes               = m_gpu.getDisplayModePropertiesKHR(m_display.displayKHR);
  m_display.modeProperties = modes[0];
  for(auto& m : modes)
  {
    auto ires = m.parameters.visibleRegion;
    auto ifreq = m.parameters.refreshRate;
    auto cres = m_display.modeProperties.parameters.visibleRegion;
    auto cfreq = m_display.modeProperties.parameters.refreshRate;
    if(ires.height * ires.width + ifreq > cres.height * cres.width + cfreq )
    {
      m_display.modeProperties = m;
    }
  }

  // pick first compatible plane
  auto     planes = m_gpu.getDisplayPlanePropertiesKHR();
  uint32_t planeIndex;
  bool     foundPlane = false;
  for(uint32_t i = 0; i < planes.size(); ++i)
  {
    auto p = planes[i];

    // skip planes bound to different display
    if(p.currentDisplay && (p.currentDisplay != m_display.displayKHR))
    {
      continue;
    }

    auto supportedDisplays = m_gpu.getDisplayPlaneSupportedDisplaysKHR(i);

    for(auto& d : supportedDisplays)
    {
      if(d == m_display.displayKHR)
      {
        foundPlane = true;
        planeIndex = i;
        break;
      }
    }

    if(foundPlane)
    {
      break;
    }
  }

  if(!foundPlane)
  {
    throw std::exception("Could not find a compatible display plane!");
  }

  // find alpha mode bit
  auto planeCapabilities = m_gpu.getDisplayPlaneCapabilitiesKHR(m_display.modeProperties.displayMode, planeIndex);
  vk::DisplayPlaneAlphaFlagBitsKHR alphaMode     = vk::DisplayPlaneAlphaFlagBitsKHR::eOpaque;
  vk::DisplayPlaneAlphaFlagBitsKHR alphaModes[4] = {vk::DisplayPlaneAlphaFlagBitsKHR::eOpaque,
                                                    vk::DisplayPlaneAlphaFlagBitsKHR::eGlobal,
                                                    vk::DisplayPlaneAlphaFlagBitsKHR::ePerPixel,
                                                    vk::DisplayPlaneAlphaFlagBitsKHR::ePerPixelPremultiplied};
  for(uint32_t i = 0; i < sizeof(alphaModes); i++)
  {
    if(planeCapabilities.supportedAlpha & alphaModes[i])
    {
      alphaMode = alphaModes[i];
      break;
    }
  }

  vk::DisplaySurfaceCreateInfoKHR surfaceCreateInfo{vk::DisplaySurfaceCreateFlagBitsKHR(),
                                                    m_display.modeProperties.displayMode,
                                                    planeIndex,
                                                    planes[planeIndex].currentStackIndex,
                                                    vk::SurfaceTransformFlagBitsKHR::eIdentity,
                                                    1.0f,
                                                    alphaMode,
                                                    vk::Extent2D(m_display.modeProperties.parameters.visibleRegion.width,
                                                                 m_display.modeProperties.parameters.visibleRegion.height)};

  m_surface = m_instance->createDisplayPlaneSurfaceKHRUnique(surfaceCreateInfo);

  const auto& d = m_display.displayProperties;
  LOGOK("Using display: %s\n  physical resolution: %i x %i\n", d.displayName, d.physicalResolution.width,
        d.physicalResolution.height);

  const auto& m = m_display.modeProperties;
  LOGOK("Display mode: %i x %i @ %fHz\n", m.parameters.visibleRegion.width, m.parameters.visibleRegion.height,
        m.parameters.refreshRate / 1000.0f);
}

void VKDirectDisplay::createLogicalDevice()
{
  // find graphics and present queue(s)
  auto families = m_gpu.getQueueFamilyProperties();
  bool found    = false;
  for(uint32_t i = 0; i < families.size(); ++i)
  {
    if((families[i].queueFlags & vk::QueueFlagBits::eGraphics) && (m_gpu.getSurfaceSupportKHR(i, m_surface.get())))
    {
      // RFE: implement support for different (graphics != present) families
      m_presentFamily = i;
      found           = true;
    }
  }

  if(!found)
  {
    throw std::exception("failed to find suitable queue family");
  }

  float priority = 1.0f;

  vk::DeviceQueueCreateInfo queueCreateInfo{vk::DeviceQueueCreateFlags(), m_presentFamily, 1, &priority};

  vk::PhysicalDeviceFeatures deviceFeatures = m_gpu.getFeatures();  

  // create the logical device and the present queue
  vk::DeviceCreateInfo deviceCreateInfo{vk::DeviceCreateFlags(),
                                        1,
                                        &queueCreateInfo,
                                        0,
                                        nullptr,
                                        uint32_t(requiredDeviceExtensions.size()),
                                        requiredDeviceExtensions.data(),
                                        &deviceFeatures};

  m_device       = m_gpu.createDeviceUnique(deviceCreateInfo);
  m_presentQueue = m_device->getQueue(m_presentFamily, 0);

  load_VK_EXTENSIONS(m_instance.get(), vkGetInstanceProcAddr, m_device.get(), vkGetDeviceProcAddr);
}

void VKDirectDisplay::createCommandPool()
{
    // create command pool
    vk::CommandPoolCreateInfo commandPoolCreateInfo = { vk::CommandPoolCreateFlags(), m_presentFamily };
    m_commandPool = m_device->createCommandPoolUnique(commandPoolCreateInfo);
}

void VKDirectDisplay::createSwapchain()
{
  auto formats      = m_gpu.getSurfaceFormatsKHR(m_surface.get());
  auto capabilities = m_gpu.getSurfaceCapabilitiesKHR(m_surface.get());
  auto presentModes = m_gpu.getSurfacePresentModesKHR(m_surface.get());

  // image count depending on capabilities
  uint32_t imageCount = std::min(capabilities.maxImageCount, capabilities.minImageCount + 1);

  // pick a preferred format or use the first available one
  vk::SurfaceFormatKHR format{vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear};
  bool                 valid = false;
  if(formats.size() == 1 && formats[0].format == vk::Format::eUndefined)
  {
    valid = true;
  }
  for(auto& f : formats)
  {
    if(f == format)
    {
      valid = true;
      break;
    }
  }
  if(!valid)
  {
    format = formats[0];
  }
  
  // use valid extent if available, otherwise derive from display mode
  vk::Extent2D extent;
  if(capabilities.currentExtent.width == 0xFFFFFFFF)
  {
    extent        = m_display.modeProperties.parameters.visibleRegion;

    auto clamp = [](int val, int min, int max) { return (val < min) ? min : (val > max) ? max : val; };
    extent.width  = clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    extent.height = clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
  }
  else
  {
    extent = capabilities.currentExtent;
  }

  vk::SurfaceTransformFlagBitsKHR pretransform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
  if((pretransform & capabilities.supportedTransforms) != pretransform)
  {
    pretransform = capabilities.currentTransform;
  }

  // pick a preferred present mode or use fallback
  vk::PresentModeKHR presentMode = vk::PresentModeKHR::eFifo;
  for(auto& m : presentModes)
  {
    if(m == vk::PresentModeKHR::eMailbox)
    {
      presentMode = m;
    }
  }

  // VK_KHR_display
  // create swapchain using the ddisplay surface created before

  vk::SwapchainCreateInfoKHR swapchainCreateInfo{vk::SwapchainCreateFlagsKHR(),
                                                 m_surface.get(),
                                                 imageCount,
                                                 format.format,
                                                 format.colorSpace,
                                                 extent,
                                                 1,
                                                 vk::ImageUsageFlags(vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst),
                                                 vk::SharingMode::eExclusive,
                                                 0,
                                                 nullptr,
                                                 pretransform,
                                                 vk::CompositeAlphaFlagBitsKHR::eOpaque,
                                                 presentMode,
                                                 VK_TRUE};

  m_swapchain       = m_device->createSwapchainKHRUnique(swapchainCreateInfo);
  m_swapchainImages = m_device->getSwapchainImagesKHR(m_swapchain.get());
  m_swapchainExtent = extent;
  m_swapchainFormat = format.format;

  // don't need to transition swapchain images from eUndefined here
}

uint32_t VKDirectDisplay::findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties)
{
  vk::PhysicalDeviceMemoryProperties memProperties = m_gpu.getMemoryProperties();

  for(uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
  {
    if((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
    {
      return i;
    }
  }
  throw std::runtime_error("failed to find suitable memory type!");
}

void VKDirectDisplay::createInteropTexture(VKGLSyncData& s)
{
  // create a VK texture and fill the GL interop data

  // vk image, hint we want to export this memory (eOpaqueWin32)
  vk::ImageCreateInfo imageCreateInfo = {vk::ImageCreateFlags(),
                                         vk::ImageType::e2D,
                                         vk::Format::eR8G8B8A8Unorm,
                                         vk::Extent3D(m_swapchainExtent, 1),
                                         1,
                                         1,
                                         vk::SampleCountFlagBits::e1,
                                         vk::ImageTiling::eOptimal,
                                         vk::ImageUsageFlags(vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc),
                                         vk::SharingMode::eExclusive,
                                         0,
                                         nullptr,
                                         vk::ImageLayout::eUndefined};
  vk::ExternalMemoryImageCreateInfo externalMemoryImageCreateInfo = { vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32 };
  imageCreateInfo.setPNext(&externalMemoryImageCreateInfo);
  s.m_image = m_device->createImageUnique(imageCreateInfo);

  vk::MemoryRequirements memoryRequirements = m_device->getImageMemoryRequirements(s.m_image.get());
  vk::MemoryAllocateInfo memoryAllocateInfo{memoryRequirements.size,
                                            findMemoryType(memoryRequirements.memoryTypeBits, vk::MemoryPropertyFlags())};

  // vk memory, also hint we want to export it
  vk::ExportMemoryAllocateInfo exportMemoryAllocateInfo(vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32);
  memoryAllocateInfo.setPNext(&exportMemoryAllocateInfo);

  vk::MemoryPriorityAllocateInfoEXT memoryPriorityAllocateInfo(1.0f);
  exportMemoryAllocateInfo.setPNext(&memoryPriorityAllocateInfo);

  s.m_deviceMemory = m_device->allocateMemoryUnique(memoryAllocateInfo);

  m_device->bindImageMemory(s.m_image.get(), s.m_deviceMemory.get(), 0);

  // transition image from eUndefined to vColorAttachmentOptimal
  auto buf = createTmpCmdBuffer();
  transitionImage(buf, s.m_image.get(),
    vk::AccessFlagBits::eNone,
    vk::AccessFlagBits::eColorAttachmentWrite,
    vk::ImageLayout::eUndefined,
    vk::ImageLayout::eColorAttachmentOptimal,
    vk::PipelineStageFlagBits::eColorAttachmentOutput,
    vk::PipelineStageFlagBits::eColorAttachmentOutput
  );
  submitTmpCmdBuffer(buf);

  // create OpenGL interop data
  vk::MemoryGetWin32HandleInfoKHR getHandleInfo{ s.m_deviceMemory.get(), vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32 };
  s.m_handle = m_device->getMemoryWin32HandleKHR(getHandleInfo);

  glCreateMemoryObjectsEXT(1, &s.m_memoryObject);
  glImportMemoryWin32HandleEXT(s.m_memoryObject, memoryRequirements.size, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, s.m_handle);

  glCreateTextures(GL_TEXTURE_2D, 1, &s.m_textureGL);
  glTextureStorageMem2DEXT(s.m_textureGL, 1, GL_RGBA8, m_swapchainExtent.width, m_swapchainExtent.height, s.m_memoryObject, 0);

  GLint internalFormat;
  glGetTextureLevelParameteriv(s.m_textureGL, 0, GL_TEXTURE_INTERNAL_FORMAT, &internalFormat);
}

void VKDirectDisplay::createInteropSemaphores(VKGLSyncData& s)
{
  // create VK semaphores and fill the GL interop data
  vk::SemaphoreCreateInfo       createInfo{};
  vk::ExportSemaphoreCreateInfo exportCreateInfo{vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueWin32};
  createInfo.setPNext(&exportCreateInfo);

  auto makeSemaphore = [&](vk::UniqueSemaphore& s, HANDLE& h, GLuint& g) {
    s = m_device->createSemaphoreUnique(createInfo);
    vk::SemaphoreGetWin32HandleInfoKHR getHandleInfo( s.get(), vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueWin32 );

    h = m_device->getSemaphoreWin32HandleKHR(getHandleInfo);
    glGenSemaphoresEXT(1, &g);
    glImportSemaphoreWin32HandleEXT(g, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, h);
  };

  makeSemaphore(s.m_available, s.m_availableHandle, s.m_availableGL);
  makeSemaphore(s.m_finished, s.m_finishedHandle, s.m_finishedGL);
}

void VKDirectDisplay::createSyncObjects()
{
  m_syncData.resize(m_swapchainImages.size());
  for(auto& s : m_syncData)
  {
    // we have to create our own textures for interop, swapchain images can't be used
    createInteropTexture(s);

    // add semaphores to signal texture ready and render ready
    createInteropSemaphores(s);

    // signal the 'available' semaphore, the interop textures aren't in use yet
    vk::SubmitInfo submitInfo{ {},{},{}, s.m_available.get() };
    m_presentQueue.submit(submitInfo);
  }
}

void VKDirectDisplay::createSyncs()
{
  vk::SemaphoreCreateInfo semaphoreCreateInfo{};
  m_imageAcquiredSemaphores.resize(m_swapchainImages.size());
  for(auto& s : m_imageAcquiredSemaphores)
  {
    s = m_device->createSemaphoreUnique(semaphoreCreateInfo);
  }

  m_blitFinishedSemaphores.resize(m_swapchainImages.size());
  for(auto& s : m_blitFinishedSemaphores)
  {
    s = m_device->createSemaphoreUnique(semaphoreCreateInfo);
  }

  vk::FenceCreateInfo fenceCreateInfo{};
  fenceCreateInfo.setFlags(vk::FenceCreateFlagBits::eSignaled);
  m_fences.resize(m_swapchainImages.size());
  for (auto& f : m_fences)
  {
    f = m_device->createFenceUnique(fenceCreateInfo);
  }
}

void VKDirectDisplay::createCommandBuffers()
{
  vk::CommandBufferAllocateInfo commandBufferAllocateInfo = {m_commandPool.get(), vk::CommandBufferLevel::ePrimary,
                                                             uint32_t(m_swapchainImages.size())};

  m_blitCommandBuffers = m_device->allocateCommandBuffers(commandBufferAllocateInfo);

  for (auto i = 0; i < m_swapchainImages.size(); ++i)
  {
    auto& swapImg = m_swapchainImages[i];
    auto& syncImg = m_syncData[i].m_image.get();
    auto& buf = m_blitCommandBuffers[i];

    vk::CommandBufferBeginInfo commandBufferBeginInfo {};
    buf.begin(commandBufferBeginInfo);

    transitionImage(
      buf, swapImg,
      vk::AccessFlagBits::eMemoryRead,
      vk::AccessFlagBits::eTransferWrite,
      vk::ImageLayout::eUndefined,        // we'll blit to it, no interest in contents
      vk::ImageLayout::eTransferDstOptimal,
      vk::PipelineStageFlagBits::eColorAttachmentOutput,
      vk::PipelineStageFlagBits::eTransfer
    );

    transitionImage(
      buf, syncImg,
      vk::AccessFlagBits::eColorAttachmentWrite,
      vk::AccessFlagBits::eTransferRead,
      vk::ImageLayout::eColorAttachmentOptimal,
      vk::ImageLayout::eTransferSrcOptimal,
      vk::PipelineStageFlagBits::eColorAttachmentOutput,
      vk::PipelineStageFlagBits::eTransfer
    );

    // dstOffsets are flipped because GL is flipped vs VK
    std::array<vk::Offset3D, 2> srcoffsets{ vk::Offset3D{ 0,0,0 }, vk::Offset3D{ int32_t(m_swapchainExtent.width), int32_t(m_swapchainExtent.height), 1 } };
    std::array<vk::Offset3D, 2> dstoffsets{ vk::Offset3D{ 0,int32_t(m_swapchainExtent.height),0 }, vk::Offset3D{ int32_t(m_swapchainExtent.width), 0, 1 } };
    vk::ImageSubresourceLayers layers{ vk::ImageAspectFlags{vk::ImageAspectFlagBits::eColor}, 0, 0, 1 };
    vk::ImageBlit region {
      layers, srcoffsets,
      layers, dstoffsets
    };
    std::vector<vk::ImageBlit> regions = { region };
    buf.blitImage(syncImg, vk::ImageLayout::eTransferSrcOptimal, swapImg, vk::ImageLayout::eTransferDstOptimal, vk::ArrayProxy<const vk::ImageBlit>{ 1, &region }, vk::Filter::eNearest);
    
    transitionImage(
      buf, swapImg,
      vk::AccessFlagBits::eTransferWrite,
      vk::AccessFlagBits::eNone,
      vk::ImageLayout::eTransferDstOptimal, 
      vk::ImageLayout::ePresentSrcKHR,
      vk::PipelineStageFlagBits::eTransfer,
      vk::PipelineStageFlagBits::eBottomOfPipe
    );

    transitionImage(
      buf, syncImg,
      vk::AccessFlagBits::eTransferRead,
      vk::AccessFlagBits::eColorAttachmentWrite,
      vk::ImageLayout::eTransferSrcOptimal,
      vk::ImageLayout::eColorAttachmentOptimal,
      vk::PipelineStageFlagBits::eTransfer,
      vk::PipelineStageFlagBits::eColorAttachmentOutput
    );
    
    buf.end();
  }
}

vk::CommandBuffer VKDirectDisplay::createTmpCmdBuffer()
{
  vk::CommandBufferAllocateInfo allocInfo{ m_commandPool.get(), vk::CommandBufferLevel::ePrimary, 1};
  vk::CommandBuffer buf;
  buf = m_device->allocateCommandBuffers(allocInfo)[0];

  vk::CommandBufferBeginInfo beginInfo{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit };
  buf.begin(beginInfo);
  return buf;
}

void VKDirectDisplay::submitTmpCmdBuffer(vk::CommandBuffer buf)
{
  buf.end();

  vk::SubmitInfo submitInfo{ {},{},buf };
  
  m_presentQueue.submit(submitInfo);
  m_presentQueue.waitIdle();
  m_device->freeCommandBuffers(m_commandPool.get(), buf);
}

void VKDirectDisplay::transitionImage(vk::CommandBuffer buf, vk::Image img, vk::AccessFlags srcAccess, vk::AccessFlags dstAccess, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::PipelineStageFlagBits srcStage, vk::PipelineStageFlags dstStage)
{
  vk::ImageMemoryBarrier barrier{};
  barrier.setSrcAccessMask(srcAccess);
  barrier.setDstAccessMask(dstAccess);
  barrier.setOldLayout(oldLayout);
  barrier.setNewLayout(newLayout);
  barrier.setImage(img);
  barrier.setSubresourceRange({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 });
  buf.pipelineBarrier(
    /*srcStageMask        */srcStage,
    /*dstStageMask        */dstStage,
    /*dependencyFlags     */{},
    /*memoryBarriers      */{},
    /*bufferMemoryBarriers*/{},
    /*imageMemoryBarriers */barrier
  );
}



