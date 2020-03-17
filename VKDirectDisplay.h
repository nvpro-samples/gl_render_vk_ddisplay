/* Copyright (c) 2014-2020, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

