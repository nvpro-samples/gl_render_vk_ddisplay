/*
 * Copyright (c) 2014-2021, NVIDIA CORPORATION.  All rights reserved.
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
 * SPDX-FileCopyrightText: Copyright (c) 2014-2021 NVIDIA CORPORATION
 * SPDX-License-Identifier: Apache-2.0
 */


 /* Contact iesser@nvidia.com (Ingo Esser) for feedback */

#include "VKDDisplay.h"

#include <algorithm>
#include <iostream>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE


typedef VkResult(*PFN_vkAcquireWinrtDisplayNV)(VkPhysicalDevice physicalDevice, VkDisplayKHR display);
PFN_vkAcquireWinrtDisplayNV pfn_vkAcquireWinrtDisplayNV = nullptr;

// required instance extenstions
const std::vector<const char*> requiredInstanceExtensions = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_DISPLAY_EXTENSION_NAME,
                                                             VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
                                                             VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME};

// required device extensions
const std::vector<const char*> requiredDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME, VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME, "VK_NV_acquire_winrt_display"};

VKDirectDisplay::VKDirectDisplay() {}

bool VKDirectDisplay::init()
{
  try
  {
    createInstance();
    pickGPU();
    createDisplaySurface();
    createLogicalDevice();
    createSwapchain();
    createSyncObjects();
    createSemaphores();
    createCommandBuffers();
    return true;
  }
  catch(std::exception const& e)
  {
    LOGE("VKDirectDisplay::init() failed: %s\n", e.what());
    return false;
  }
}

GLuint VKDirectDisplay::getTexture()
{
  // GL: wait for VK image available

  // don't wait for the first frame to be available - there's nobody to signal this
  static bool firstFrame = true;
  if (firstFrame)
  {
    firstFrame = false;
  }
  else
  {
    glWaitSemaphoreEXT(m_syncData[m_frameIndex].m_availableGL, 0, nullptr, 0, nullptr, nullptr);
  }
  return m_syncData[m_frameIndex].m_textureGL;
}

void VKDirectDisplay::submitTexture()
{
  // GL: signal rendering is done
  // VK: acquire image from swapchain
  // VK: blit texture to swapchain image (wait for GL finished, VK image acquired. signal VK blit done)
  // present (wait for VK blit done. signal VK image available)

  // signal GL is done
  glSignalSemaphoreEXT(m_syncData[m_frameIndex].m_finishedGL, 0, nullptr, 0, nullptr, nullptr);

  // RFE: handle return values
  auto r = m_device->acquireNextImageKHR(m_swapchain.get(), std::numeric_limits<uint64_t>::max(),
                                         m_imageAcquiredSemaphores[m_frameIndex].get(), vk::Fence());
  assert(m_frameIndex == r.value);  // this should be guaranteed, decoupling would mean N*M prepared blit command buffers

  // wait for GL finished & VK imageAcquired, blit/copy current texture onto current swapchain image, signal VK blit finished
  std::vector<vk::Semaphore> blitWaitSemaphores{m_syncData[m_frameIndex].m_finished.get(),
                                                m_imageAcquiredSemaphores[m_frameIndex].get()};
  std::vector<vk::Semaphore> blitSignalSemaphores{m_blitFinishedSemaphores[m_frameIndex].get()};
  vk::PipelineStageFlags blitWaitStages{ vk::PipelineStageFlagBits::eTopOfPipe };

  vk::SubmitInfo submitInfo{uint32_t(blitWaitSemaphores.size()),
                            &blitWaitSemaphores[0],
                            &blitWaitStages, 
                            1,
                            & m_blitCommandBuffers[m_frameIndex],
                            uint32_t(blitSignalSemaphores.size()),
                            & blitSignalSemaphores[0] };

  m_presentQueue.submit(submitInfo, vk::Fence{});

  // present
  std::vector<vk::Semaphore> presentWaitSemaphores{m_blitFinishedSemaphores[m_frameIndex].get()};
  std::vector<vk::Semaphore> presentSignalSemaphores{m_syncData[m_frameIndex].m_available.get()};

  vk::PresentInfoKHR presentInfo{uint32_t(presentWaitSemaphores.size()),
                                 presentWaitSemaphores.data(),
                                 1,
                                 &m_swapchain.get(),
                                 &m_frameIndex,
                                 nullptr};
  
  // VK_KHR_display
  // present on Direct Display output
  m_presentQueue.presentKHR(presentInfo);

  m_frameIndex = (m_frameIndex + 1) % m_swapchainImages.size();

  // RFE
  glSignalSemaphoreEXT(m_syncData[m_frameIndex].m_availableGL, 0, nullptr, 0, nullptr, nullptr);
}

void VKDirectDisplay::createInstance()
{
  vk::DynamicLoader         dl;
  PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr =
    dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
  VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

  // check for required instance extensions
  std::vector<vk::ExtensionProperties> availableInstanceExtensions = vk::enumerateInstanceExtensionProperties();

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
      vk::InstanceCreateFlags(),        nullptr, 0, nullptr, uint32_t(requiredInstanceExtensions.size()),
      requiredInstanceExtensions.data()};
  m_instance = vk::createInstanceUnique(createInfo);

  VULKAN_HPP_DEFAULT_DISPATCHER.init(m_instance.get());

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
      return false;
    }
  }
  return true;
}

void VKDirectDisplay::pickGPU()
{
  // pick a GPU that has the required device extensions and has a display device attached
  std::vector<vk::PhysicalDevice> devices = m_instance->enumeratePhysicalDevices();
  for(const auto& device : devices)
  {
    if(!device.getDisplayPropertiesKHR().empty() && checkDeviceExtensionSupport(device))
    {
      // VK_KHR_display
      // GPU with ddisplay found
      m_gpu = device;
      break;
    }
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

  // pick highest available resolution
  auto modes               = m_gpu.getDisplayModePropertiesKHR(m_display.displayKHR);
  m_display.modeProperties = modes[0];
  for(auto& m : modes)
  {
    auto i = m.parameters.visibleRegion;
    auto c = m_display.modeProperties.parameters.visibleRegion;
    if(i.height * i.width > c.height * c.width)
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

  VkResult res = pfn_vkAcquireWinrtDisplayNV(m_gpu, m_display.displayKHR);

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

  vk::PhysicalDeviceFeatures deviceFeatures;

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
                                                 vk::ImageUsageFlags(vk::ImageUsageFlagBits::eColorAttachment),
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
  vk::ImageCreateInfo imageCreateInfo = {vk::ImageCreateFlags(),
                                         vk::ImageType::e2D,
                                         vk::Format::eR8G8B8A8Unorm,
                                         vk::Extent3D(m_swapchainExtent, 1),
                                         1,
                                         1,
                                         vk::SampleCountFlagBits::e1,
                                         vk::ImageTiling::eOptimal,
                                         vk::ImageUsageFlags(vk::ImageUsageFlagBits::eColorAttachment),
                                         vk::SharingMode::eExclusive,
                                         0,
                                         nullptr,
                                         vk::ImageLayout::ePreinitialized};
  s.m_image                           = m_device->createImageUnique(imageCreateInfo);

  vk::MemoryRequirements memoryRequirements = m_device->getImageMemoryRequirements(s.m_image.get());
  vk::MemoryAllocateInfo memoryAllocateInfo{memoryRequirements.size,
                                            findMemoryType(memoryRequirements.memoryTypeBits, vk::MemoryPropertyFlags())};

  // pass in hint that we want to export this memory
  vk::ExportMemoryAllocateInfo exportMemoryAllocateInfo(vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32);
  memoryAllocateInfo.setPNext(&exportMemoryAllocateInfo);

  s.m_deviceMemory = m_device->allocateMemoryUnique(memoryAllocateInfo);

  m_device->bindImageMemory(s.m_image.get(), s.m_deviceMemory.get(), 0);

  // create OpenGL interop data
  VkMemoryGetWin32HandleInfoKHR getHandleInfo{VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR, nullptr,
                                              s.m_deviceMemory.get(),
                                              VkExternalMemoryHandleTypeFlagBits::VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT};
  s.m_handle =  m_device->getMemoryWin32HandleKHR(getHandleInfo);
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
    VkSemaphoreGetWin32HandleInfoKHR getHandleInfo{VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR, nullptr, s.get(),
                                                   VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT};
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
    // we have to create our own textures for interop, swapchain images can't be used (boooo!)
    createInteropTexture(s);

    // add semaphores to signal texture ready and render ready
    createInteropSemaphores(s);
  }
}

void VKDirectDisplay::createSemaphores()
{
  vk::SemaphoreCreateInfo createInfo{};

  m_imageAcquiredSemaphores.resize(m_swapchainImages.size());
  for(auto& s : m_imageAcquiredSemaphores)
  {
    s = m_device->createSemaphoreUnique(createInfo);
  }

  m_blitFinishedSemaphores.resize(m_swapchainImages.size());
  for(auto& s : m_blitFinishedSemaphores)
  {
    s = m_device->createSemaphoreUnique(createInfo);
  }
}

void VKDirectDisplay::createCommandBuffers()
{
  // create command pool
  vk::CommandPoolCreateInfo commandPoolCreateInfo = {vk::CommandPoolCreateFlags(), m_presentFamily};
  m_commandPool                                   = m_device->createCommandPool(commandPoolCreateInfo);

  vk::CommandBufferAllocateInfo commandBufferAllocateInfo = {m_commandPool, vk::CommandBufferLevel::ePrimary,
                                                             uint32_t(m_swapchainImages.size())};

  m_blitCommandBuffers = m_device->allocateCommandBuffers(commandBufferAllocateInfo);

  for (auto i = 0; i < m_swapchainImages.size(); ++i)
  {
    vk::CommandBufferBeginInfo commandBufferBeginInfo {};
    auto& b = m_blitCommandBuffers[i];
    b.begin(commandBufferBeginInfo);
    std::array<vk::Offset3D, 2> srcoffsets{ vk::Offset3D{ 0,0,0 }, vk::Offset3D{ int32_t(m_swapchainExtent.width), int32_t(m_swapchainExtent.height), 0 } };
    std::array<vk::Offset3D, 2> dstoffsets{ vk::Offset3D{ 0,int32_t(m_swapchainExtent.height),0 }, vk::Offset3D{ int32_t(m_swapchainExtent.width), 0, 0 } };
    vk::ImageSubresourceLayers layers{ vk::ImageAspectFlags{vk::ImageAspectFlagBits::eColor}, 0, 0, 1 };
    vk::ImageBlit region {
      layers, srcoffsets,
      layers, dstoffsets
    };
    std::vector<vk::ImageBlit> regions = { region };
    b.blitImage(m_syncData[i].m_image.get(), vk::ImageLayout::eTransferSrcOptimal, m_swapchainImages[i], vk::ImageLayout::eTransferDstOptimal, vk::ArrayProxy<const vk::ImageBlit>{ 1, &region }, vk::Filter::eNearest);
    b.end();
  }
}
