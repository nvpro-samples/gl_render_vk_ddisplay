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


#pragma once

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1

#include <include_gl.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_win32.h>
#include <nvvk/extensions_vk.hpp>

#include <nvh/nvprint.hpp>

#include <vector>

class VKDirectDisplay
{
public:
  VKDirectDisplay();

  // initialize direct display and GL textures
  // call this with the GL context current that's used for interop 
  bool init();

  // width and height of swapchain interop textures
  // by default the highest resolution available for the direct display
  uint32_t getWidth()  { return m_swapchainExtent.width; }
  uint32_t getHeight() { return m_swapchainExtent.height; }

  // get the texture to render the next frame into
  // synchronization: GL waits for the VK texture to be available
  GLuint getTexture();

  // submit this texture to the direct display
  // synchronization:
  // * GL signals to VK that rendering is done
  // * VK signals to VK that texture can be used for next frame
  void submitTexture();

private:

  struct Display
  {
    vk::DisplayKHR               displayKHR;
    vk::DisplayPropertiesKHR     displayProperties;
    vk::DisplayModePropertiesKHR modeProperties;
  };

  struct VKGLSyncData
  {
    // VK texture
    vk::UniqueImage         m_image;
    vk::UniqueDeviceMemory  m_deviceMemory;
    HANDLE                  m_handle;
    GLuint                  m_memoryObject;

    // GL texture
    GLuint                  m_textureGL;

    // VK semaphores
    vk::UniqueSemaphore m_available; // signal when image is available
    vk::UniqueSemaphore m_finished;  // wait for GL to be finished
    HANDLE              m_availableHandle;
    HANDLE              m_finishedHandle;

    // GL semaphores
    GLuint              m_availableGL;
    GLuint              m_finishedGL;
  };

  vk::UniqueInstance                m_instance;
  vk::PhysicalDevice                m_gpu;
  Display                           m_display;
  vk::UniqueSurfaceKHR              m_surface;
  uint32_t                          m_presentFamily{ 0 };
  vk::Queue                         m_presentQueue;
  vk::UniqueDevice                  m_device;
  vk::UniqueSwapchainKHR            m_swapchain;
  std::vector<vk::Image>            m_swapchainImages;
  vk::Extent2D                      m_swapchainExtent;
  vk::Format                        m_swapchainFormat{ vk::Format::eUndefined };
  uint32_t                          m_frameIndex{ 0 };
  std::vector<VKGLSyncData>         m_syncData;
  std::vector<vk::UniqueSemaphore>  m_imageAcquiredSemaphores;
  std::vector<vk::UniqueSemaphore>  m_blitFinishedSemaphores;
  vk::CommandPool                   m_commandPool;
  std::vector<vk::CommandBuffer>    m_blitCommandBuffers;

  void createInstance();
  bool checkDeviceExtensionSupport(vk::PhysicalDevice device);
  void pickGPU();
  void createDisplaySurface();
  void createLogicalDevice();
  void createSwapchain();
  uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);
  void createInteropTexture(VKGLSyncData& s);
  void createInteropSemaphores(VKGLSyncData& s);
  void createSyncObjects();
  void createSemaphores();
  void createCommandBuffers();
};

