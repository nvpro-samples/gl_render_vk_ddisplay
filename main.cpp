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


#define NVGLF_DEBUG_FILTER 1

#include <include_gl.h>
#include <windows.h>

#include <imgui/imgui_helper.h>
#include <imgui/imgui_impl_gl.h>

#include <nvgl/appwindowprofiler_gl.hpp>
#include <nvgl/base_gl.hpp>
#include <nvgl/programmanager_gl.hpp>
#include <nvh/cameracontrol.hpp>
#include <nvh/geometry.hpp>
#include <nvmath/nvmath_glsltypes.h>

#include <array>
#include <chrono>
#include <iostream>
#include <locale>
#include <map>
#include <sstream>
#include <thread>

#include "common.h"
#include "VKDirectDisplay.h"

namespace {
int const SAMPLE_SIZE_WIDTH  = 800;
int const SAMPLE_SIZE_HEIGHT = 600;

int const SAMPLE_MAJOR_VERSION = 4;
int const SAMPLE_MINOR_VERSION = 5;
}  // namespace




namespace render {

struct UIData
{
  bool  m_drawUI       = true;
  int   m_texWidth     = SAMPLE_SIZE_WIDTH;
  int   m_texHeight    = SAMPLE_SIZE_HEIGHT;
  float m_vertexLoad   = 42.0f;
  int   m_fragmentLoad = 10;

  int   m_torus_n       = 420;
  int   m_torus_m       = 420;
  float m_numTriangles  = 0.0f;
  float m_numTrisPerSec = 0.0f;
  float m_fps           = 0.0f;
  bool  m_profilerPrint = true;
};

struct Vertex
{
  Vertex(const nvh::geometry::Vertex& vertex)
  {
    position = vertex.position;
    normal   = vertex.normal;
    color    = nvmath::vec4(1.0f);
  }

  nvmath::vec4 position;
  nvmath::vec4 normal;
  nvmath::vec4 color;
};

struct Buffers
{
  Buffers()
      : vbo(0)
      , ibo(0)
      , sceneUbo(0)
      , objectUbo(0)
      , composeUbo(0)
      , numVertices(0)
      , numIndices(0)
  {
  }

  GLuint vbo;
  GLuint ibo;
  GLuint sceneUbo;
  GLuint objectUbo;
  GLuint composeUbo;

  GLsizei numVertices;
  GLsizei numIndices;
};

struct Textures
{
  GLuint colorTex;
  GLuint depthTex;
};

struct Programs
{
  nvgl::ProgramID scene;
  nvgl::ProgramID compose;
};

struct Data
{
  UIData           uiData;
  UIData           lastUIData;
  ImGuiH::Registry ui;
  double           uiTime = 0;

  Buffers  buf;
  Textures tex;
  Programs prog;

  SceneData   sceneData;
  ObjectData  objectData;
  ComposeData composeData;

  GLuint renderFBO = 0;

  nvgl::ProgramManager pm;

  int windowWidth  = SAMPLE_SIZE_WIDTH;
  int windowHeight = SAMPLE_SIZE_HEIGHT;
};

auto initPrograms(Data& rd) -> bool
{
  nvgl::ProgramManager& pm       = rd.pm;
  Programs&             programs = rd.prog;

  bool validated(true);

  pm.addDirectory(std::string(PROJECT_NAME));
  pm.addDirectory(NVPSystem::exePath() + std::string(PROJECT_RELDIRECTORY));

  pm.registerInclude("common.h", "common.h");
  pm.registerInclude("noise.glsl", "noise.glsl");

  {
    programs.scene = pm.createProgram(
        nvgl::ProgramManager::Definition(GL_VERTEX_SHADER, "#define USE_SCENE_DATA", "scene.vert.glsl"),
        nvgl::ProgramManager::Definition(GL_FRAGMENT_SHADER, "#define USE_SCENE_DATA", "scene.frag.glsl"));
    programs.compose = pm.createProgram(
        nvgl::ProgramManager::Definition(GL_VERTEX_SHADER, "#define USE_COMPOSE_DATA", "compose.vert.glsl"),
        nvgl::ProgramManager::Definition(GL_FRAGMENT_SHADER, "#define USE_COMPOSE_DATA", "compose.frag.glsl"));
  }

  validated = pm.areProgramsValid();
  return validated;
}

auto initFBOs(Data& rd) -> void
{
  nvgl::newFramebuffer(rd.renderFBO);
}

auto initBuffers(Data& rd) -> void
{
  Buffers& buffers = rd.buf;

  // Torus geometry
  {
    unsigned int m           = rd.uiData.m_torus_m;
    unsigned int n           = rd.uiData.m_torus_n;
    float        innerRadius = 0.8f;
    float        outerRadius = 0.2f;

    std::vector<nvmath::vec3> vertices;
    std::vector<nvmath::vec3> tangents;
    std::vector<nvmath::vec3> binormals;
    std::vector<nvmath::vec3> normals;
    std::vector<nvmath::vec2> texcoords;
    std::vector<unsigned int> indices;

    unsigned int size_v = (m + 1) * (n + 1);

    vertices.reserve(size_v);
    tangents.reserve(size_v);
    binormals.reserve(size_v);
    normals.reserve(size_v);
    texcoords.reserve(size_v);
    indices.reserve(6 * m * n);

    float mf = (float)m;
    float nf = (float)n;

    float phi_step   = 2.0f * nv_pi / mf;
    float theta_step = 2.0f * nv_pi / nf;

    // Setup vertices and normals
    // Generate the Torus exactly like the sphere with rings around the origin along the latitudes.
    for(unsigned int latitude = 0; latitude <= n; latitude++)  // theta angle
    {
      float theta    = (float)latitude * theta_step;
      float sinTheta = sinf(theta);
      float cosTheta = cosf(theta);

      float radius = innerRadius + outerRadius * cosTheta;

      for(unsigned int longitude = 0; longitude <= m; longitude++)  // phi angle
      {
        float phi    = (float)longitude * phi_step;
        float sinPhi = sinf(phi);
        float cosPhi = cosf(phi);

        vertices.push_back(nvmath::vec3(radius * cosPhi, outerRadius * sinTheta, radius * -sinPhi));

        tangents.push_back(nvmath::vec3(-sinPhi, 0.0f, -cosPhi));

        binormals.push_back(nvmath::vec3(cosPhi * -sinTheta, cosTheta, sinPhi * sinTheta));

        normals.push_back(nvmath::vec3(cosPhi * cosTheta, sinTheta, -sinPhi * cosTheta));

        texcoords.push_back(nvmath::vec2((float)longitude / mf, (float)latitude / nf));
      }
    }

    const unsigned int columns = m + 1;

    // Setup indices
    for(unsigned int latitude = 0; latitude < n; latitude++)
    {
      for(unsigned int longitude = 0; longitude < m; longitude++)
      {
        // two triangles
        indices.push_back(latitude * columns + longitude);        // lower left
        indices.push_back(latitude * columns + longitude + 1);    // lower right
        indices.push_back((latitude + 1) * columns + longitude);  // upper left

        indices.push_back((latitude + 1) * columns + longitude);      // upper left
        indices.push_back(latitude * columns + longitude + 1);        // lower right
        indices.push_back((latitude + 1) * columns + longitude + 1);  // upper right
      }
    }

    buffers.numVertices                        = static_cast<GLsizei>(vertices.size());
    GLsizeiptr const sizePositionAttributeData = vertices.size() * sizeof(vertices[0]);
    GLsizeiptr const sizeNormalAttributeData   = normals.size() * sizeof(normals[0]);
    GLsizeiptr const sizeTexAttributeData      = texcoords.size() * sizeof(texcoords[0]);

    buffers.numIndices       = static_cast<GLsizei>(indices.size());
    GLsizeiptr sizeIndexData = indices.size() * sizeof(indices[0]);

    nvgl::newBuffer(buffers.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, buffers.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizePositionAttributeData + sizeNormalAttributeData + sizeTexAttributeData, nullptr, GL_STATIC_DRAW);
    GLsizeiptr off = 0;
    glBufferSubData(GL_ARRAY_BUFFER, off, sizePositionAttributeData, &vertices[0]);
    off += sizePositionAttributeData;
    glBufferSubData(GL_ARRAY_BUFFER, off, sizeNormalAttributeData, &normals[0]);
    off += sizeNormalAttributeData;
    glBufferSubData(GL_ARRAY_BUFFER, off, sizeTexAttributeData, &texcoords[0]);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    nvgl::newBuffer(buffers.ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeIndexData, &indices[0], GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  }

  nvgl::newBuffer(buffers.sceneUbo);
  glBindBuffer(GL_UNIFORM_BUFFER, buffers.sceneUbo);
  glBufferData(GL_UNIFORM_BUFFER, sizeof(SceneData), nullptr, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);

  nvgl::newBuffer(buffers.objectUbo);
  glBindBuffer(GL_UNIFORM_BUFFER, buffers.objectUbo);
  glBufferData(GL_UNIFORM_BUFFER, sizeof(ObjectData), nullptr, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);

  nvgl::newBuffer(buffers.composeUbo);
  glBindBuffer(GL_UNIFORM_BUFFER, buffers.composeUbo);
  glBufferData(GL_UNIFORM_BUFFER, sizeof(ComposeData), nullptr, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

auto initTextures(Data& rd) -> void
{
  auto newTex = [&](GLuint& tex) {
    nvgl::newTexture(tex, GL_TEXTURE_2D);
    nvgl::bindMultiTexture(GL_TEXTURE0, GL_TEXTURE_2D, tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, rd.uiData.m_texWidth, rd.uiData.m_texHeight);
    nvgl::bindMultiTexture(GL_TEXTURE0, GL_TEXTURE_2D, 0);
  };

  newTex(rd.tex.colorTex);

  nvgl::newTexture(rd.tex.depthTex, GL_TEXTURE_2D);
  nvgl::bindMultiTexture(GL_TEXTURE0, GL_TEXTURE_2D, rd.tex.depthTex);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH_COMPONENT24, rd.uiData.m_texWidth, rd.uiData.m_texHeight);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
  nvgl::bindMultiTexture(GL_TEXTURE0, GL_TEXTURE_2D, 0);
}

auto renderTori(Data& rd, float numTori, size_t width, size_t height, nvmath::mat4f view) -> void
{
  float num = ceil(numTori);

  // bind geometry
  glBindBuffer(GL_ARRAY_BUFFER, rd.buf.vbo);
  glVertexAttribPointer(VERTEX_POS, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), 0);
  glVertexAttribPointer(VERTEX_NORMAL, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (GLvoid*)(rd.buf.numVertices * 3 * sizeof(float)));
  glVertexAttribPointer(VERTEX_TEX, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (GLvoid*)(rd.buf.numVertices * 6 * sizeof(float)));

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, rd.buf.ibo);

  glEnableVertexAttribArray(VERTEX_POS);
  glEnableVertexAttribArray(VERTEX_NORMAL);
  glEnableVertexAttribArray(VERTEX_TEX);

  // distribute num tori into an numX x numY pattern
  // with numX * numY > num, numX = aspect * numY

  float aspect = (float)width / (float)height;

  size_t numX = static_cast<size_t>(ceil(sqrt(num * aspect)));
  size_t numY = static_cast<size_t>((float)numX / aspect);
  if(numX * numY < num)
  {
    ++numY;
  }
  //size_t numY = static_cast<size_t>( ceil(sqrt(num / aspect)) );
  float rx = 1.0f;  // radius of ring
  float ry = 1.0f;
  float dx = 1.0f;  // ring distance
  float dy = 1.5f;
  float sx = (numX - 1) * dx + 2 * rx;  // array size
  float sy = (numY - 1) * dy + 2 * ry;

  float x0 = -sx / 2.0f + rx;
  float y0 = -sy / 2.0f + ry;

  float scale = std::min(1.f / sx, 1.f / sy) * 0.8f;

  size_t torusIndex = 0;
  for(size_t i = 0; i < numY && torusIndex < num; ++i)
  {
    for(size_t j = 0; j < numX && torusIndex < num; ++j)
    {
      float y = y0 + i * dy;
      float x = x0 + j * dx;

      // set and upload object UBO data
      rd.objectData.model = nvmath::scale_mat4(nvmath::vec3(scale)) * nvmath::translation_mat4(nvmath::vec3(x, y, 0.0f))
                            * nvmath::rotation_mat4_x((j % 2 ? -1.0f : 1.0f) * 45.0f * nv_pi / 180.0f);
      rd.objectData.modelView     = view * rd.objectData.model;
      rd.objectData.modelViewIT   = nvmath::transpose(nvmath::invert(rd.objectData.modelView));
      rd.objectData.modelViewProj = rd.sceneData.viewProjMatrix * rd.objectData.model;
      //rd.objectData.color = nvmath::vec3f((torusIndex + 1) & 1, ((torusIndex + 1) & 2) / 2, ((torusIndex + 1) & 4) / 4);
      rd.objectData.color = nvmath::vec3f(0.0f, 0.0f, 1.0f);
      glNamedBufferSubData(rd.buf.objectUbo, 0, sizeof(ObjectData), &rd.objectData);
      glBindBufferBase(GL_UNIFORM_BUFFER, UBO_OBJECT, rd.buf.objectUbo);

      GLsizei count = 0;
      if(torusIndex < floor(numTori))
      {
        count = rd.buf.numIndices;
      }
      else
      {
        // render the fraction of the last torus
        count = GLsizei(rd.buf.numIndices * (numTori - floor(numTori)));
      }

      glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, NV_BUFFER_OFFSET(0));

      ++torusIndex;
    }
  }

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

  glDisableVertexAttribArray(VERTEX_POS);
  glDisableVertexAttribArray(VERTEX_NORMAL);
  glDisableVertexAttribArray(VERTEX_TEX);
}
}  //namespace render

class Sample : public nvgl::AppWindowProfilerGL
{
public:
  Sample();

  bool begin();
  void processUI(double time);
  void think(double time);
  void resize(int width, int height);
  void end();

  // return true to prevent m_window updates
  bool mouse_pos(int x, int y)
  {
    if(!m_rd.uiData.m_drawUI)
      return false;
    return ImGuiH::mouse_pos(x, y);
  }
  bool mouse_button(int button, int action)
  {
    if(!m_rd.uiData.m_drawUI)
      return false;
    return ImGuiH::mouse_button(button, action);
  }
  bool mouse_wheel(int wheel)
  {
    if(!m_rd.uiData.m_drawUI)
      return false;
    return ImGuiH::mouse_wheel(wheel);
  }
  bool key_char(int button)
  {
    if(!m_rd.uiData.m_drawUI)
      return false;
    return ImGuiH::key_char(button);
  }
  bool key_button(int button, int action, int mods)
  {
    if(!m_rd.uiData.m_drawUI)
      return false;
    return ImGuiH::key_button(button, action, mods);
  }

  void rebuild_geometry()
  {
    render::initBuffers(m_rd);
    LOGOK("Scene data:\n");
    LOGOK("Vertices per torus:  %i\n", m_rd.buf.numVertices);
    LOGOK("Triangles per torus: %i\n", m_rd.buf.numIndices / 3);
  };

protected:
  nvh::CameraControl m_control;
  size_t             m_frameCount;
  render::Data       m_rd;

  VKDirectDisplay m_vkdd;
};

Sample::Sample()
    : nvgl::AppWindowProfilerGL(/*singleThreaded=*/true, /*doSwap=*/true)
    , m_frameCount(0)
{
}

bool Sample::begin()
{
  ImGuiH::Init(m_windowState.m_swapSize[0], m_windowState.m_swapSize[1], this);
  ImGui::InitGL();

  setVsync(false);

  bool validated(true);

  // control setup
  m_control.m_sceneOrbit     = nvmath::vec3(0.0f);
  m_control.m_sceneDimension = 1.0f;
  m_control.m_viewMatrix = nvmath::look_at(m_control.m_sceneOrbit - nvmath::vec3f(0., 0, -m_control.m_sceneDimension),
                                           m_control.m_sceneOrbit, nvmath::vec3f(0, 1, 0));

  render::initPrograms(m_rd);
  render::initFBOs(m_rd);
  render::initBuffers(m_rd);

  LOGOK("Scene data:\n");
  LOGOK("Vertices per torus:  %i\n", m_rd.buf.numVertices);
  LOGOK("Triangles per torus: %i\n", m_rd.buf.numIndices / 3);

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glFrontFace(GL_CCW);

  // VK_KHR_display
  // initialize VK ddisplay class
  validated &= m_vkdd.init();

  m_rd.uiData.m_texWidth = m_vkdd.getWidth();
  m_rd.uiData.m_texHeight = m_vkdd.getHeight();
  
  render::initTextures(m_rd);
  
  return validated;
}

void Sample::processUI(double time)
{
  static double timeBegin = time;
  static int    frames    = 0;

  ++frames;
  double timeCurrent = time;
  double timeDelta   = timeCurrent - timeBegin;

  if(timeDelta > 1.0)
  {
    m_rd.uiData.m_fps           = (float)(frames / timeDelta);
    m_rd.uiData.m_numTriangles  = m_rd.buf.numIndices / 3 * m_rd.uiData.m_vertexLoad;
    m_rd.uiData.m_numTrisPerSec = m_rd.uiData.m_numTriangles * m_rd.uiData.m_fps;
    frames                      = 0;
    timeBegin                   = time;
  }

  int width  = m_windowState.m_swapSize[0];
  int height = m_windowState.m_swapSize[1];

  // Update imgui configuration
  auto& imgui_io       = ImGui::GetIO();
  imgui_io.DeltaTime   = static_cast<float>(time - m_rd.uiTime);
  imgui_io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));

  m_rd.uiTime = time;

  ImGui::NewFrame();
  ImGui::SetNextWindowPos(ImVec2(5, 5), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(350, 0), ImGuiCond_FirstUseEver);

  if(ImGui::Begin("NVIDIA " PROJECT_NAME, nullptr))
  {
    ImGui::PushItemWidth(150);

    // TODO: reactivate, handle change in GL and VK?
    //ImGuiH::InputIntClamped("tex w", &m_rd.uiData.m_texWidth, 10, INT_MAX, 10, 100, ImGuiInputTextFlags_EnterReturnsTrue);
    //ImGuiH::InputIntClamped("tex h", &m_rd.uiData.m_texHeight, 10, INT_MAX, 10, 100, ImGuiInputTextFlags_EnterReturnsTrue);

    ImGuiH::InputFloatClamped("vertex load", &m_rd.uiData.m_vertexLoad, 1.0f, (float)INT_MAX, 1, 10, "%.1f",
                              ImGuiInputTextFlags_EnterReturnsTrue);
    ImGuiH::InputIntClamped("fragment load", &m_rd.uiData.m_fragmentLoad, 1, INT_MAX, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::LabelText("frames / s", "%.2f", m_rd.uiData.m_fps);
    ImGui::LabelText("M triangles", "%.2f", m_rd.uiData.m_numTriangles / 1E6f);
    ImGui::LabelText("B tris / s", "%.2f", m_rd.uiData.m_numTrisPerSec / 1E9f);
  }
  ImGui::End();
}

void Sample::think(double time)
{
  processUI(time);

  // handle ui data changes
  /*
  TODO: see processUI
  if(m_rd.lastUIData.m_texWidth != m_rd.uiData.m_texWidth || m_rd.lastUIData.m_texHeight != m_rd.uiData.m_texHeight)
  {
    render::initTextures(m_rd);
  }*/

  m_rd.lastUIData = m_rd.uiData;

  // VK_KHR_display
  // obtain next render texture from VK ddisplay class
  GLuint tex = m_vkdd.getTexture();

  // depending on the algorithm the display w/h depends on window or texture size(s)
  const int displayWidth  = m_vkdd.getWidth();
  const int displayHeight = m_vkdd.getHeight();

  // setup
  nvmath::mat4f view;
  {
    NV_PROFILE_GL_SECTION("setup");
    m_profilerPrint = m_rd.uiData.m_profilerPrint;

    // handle mouse input
    m_control.processActions(m_windowState.m_swapSize,
                             nvmath::vec2f(m_windowState.m_mouseCurrent[0], m_windowState.m_mouseCurrent[1]),
                             m_windowState.m_mouseButtonFlags, m_windowState.m_mouseWheel);

    if(m_windowState.onPress(KEY_SPACE))
    {
      m_rd.uiData.m_drawUI = m_rd.uiData.m_drawUI ? 0 : 1;
    }

    ++m_frameCount;

    auto proj =
        nvmath::perspective(45.f, float(displayWidth) / float(displayHeight), m_rd.sceneData.projNear, m_rd.sceneData.projFar);

    float         depth      = 1.0f;
    nvmath::vec4f background = nvmath::vec4f(118.f / 255.f, 185.f / 255.f, 0.f / 255.f, 0.f / 255.f);

    // calculate some coordinate systems
    view                        = m_control.m_viewMatrix;
    nvmath::mat4f iview         = invert(view);
    nvmath::vec3f eyePos_world  = nvmath::vec3f(iview(0, 3), iview(1, 3), iview(2, 3));
    nvmath::vec3f eyePos_view   = view * nvmath::vec4f(eyePos_world, 1);
    nvmath::vec3f right_view    = nvmath::vec3f(1.0f, 0.0f, 0.0f);
    nvmath::vec3f up_view       = nvmath::vec3f(0.0f, 1.0f, 0.0f);
    nvmath::vec3f forward_view  = nvmath::vec3f(0.0f, 0.0f, -1.0f);
    nvmath::vec3f right_world   = iview * nvmath::vec4f(right_view, 0.0f);
    nvmath::vec3f up_world      = iview * nvmath::vec4f(up_view, 0.0f);
    nvmath::vec3f forward_world = iview * nvmath::vec4f(forward_view, 0.0f);

    // fill sceneData struct
    m_rd.sceneData.viewMatrix      = view;
    m_rd.sceneData.projMatrix      = proj;
    m_rd.sceneData.viewProjMatrix  = proj * view;
    m_rd.sceneData.lightPos_world  = eyePos_world + right_world;
    m_rd.sceneData.eyepos_world    = eyePos_world;
    m_rd.sceneData.eyePos_view     = eyePos_view;
    m_rd.sceneData.backgroundColor = background;
    m_rd.sceneData.fragmentLoad    = m_rd.uiData.m_fragmentLoad;

    // fill scene UBO
    glNamedBufferSubData(m_rd.buf.sceneUbo, 0, sizeof(SceneData), &m_rd.sceneData);
    glBindBufferBase(GL_UNIFORM_BUFFER, UBO_SCENE, m_rd.buf.sceneUbo);

    // prepare an FBO to render into, clear all textures with a dark gray
    glBindFramebuffer(GL_FRAMEBUFFER, m_rd.renderFBO);
    glViewport(0, 0, m_rd.uiData.m_texWidth, m_rd.uiData.m_texHeight);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_rd.tex.depthTex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
      LOGE("Framebuffer check failed: %i\n", glCheckFramebufferStatus(GL_FRAMEBUFFER));
    }

    glClearBufferfv(GL_COLOR, 0, &background[0]);
    glClearBufferfv(GL_DEPTH, 0, &depth);

    glUseProgram(m_rd.pm.get(m_rd.prog.scene));
  }

  {
    NV_PROFILE_GL_SECTION("render");
    // render tori into texture 
    renderTori(m_rd, m_rd.uiData.m_vertexLoad, displayWidth, displayHeight, view);
  }

  {
    NV_PROFILE_GL_SECTION("submit");
    // VK_KHR_display
    // submit rendered texture to VK ddisplay class
    m_vkdd.submitTexture();
  }

  {
    NV_PROFILE_GL_SECTION("compose");

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // render complete viewport
    glViewport(0, 0, m_rd.windowWidth, m_rd.windowHeight);
    glUseProgram(m_rd.pm.get(m_rd.prog.compose));

    // set & upload compose data
    m_rd.composeData.out_width  = m_rd.windowWidth;
    m_rd.composeData.out_height = m_rd.windowHeight;
    m_rd.composeData.in_width   = m_rd.uiData.m_texWidth;
    m_rd.composeData.in_height  = m_rd.uiData.m_texHeight;
    glNamedBufferSubData(m_rd.buf.composeUbo, 0, sizeof(ComposeData), &m_rd.composeData);
    glBindBufferBase(GL_UNIFORM_BUFFER, UBO_COMP, m_rd.buf.composeUbo);

    // use rendered texture as input textures
    nvgl::bindMultiTexture(GL_TEXTURE0, GL_TEXTURE_2D, tex);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // render one triangle covering the whole viewport
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }

  if(m_rd.uiData.m_drawUI)
  {
    NV_PROFILE_GL_SECTION("TwDraw");
    ImGui::Render();
    ImGui::RenderDrawDataGL(ImGui::GetDrawData());
  }

  ImGui::EndFrame();
}

void Sample::resize(int width, int height)
{
  m_windowState.m_swapSize[0] = width;
  m_windowState.m_swapSize[1] = height;

  m_rd.windowWidth  = width;
  m_rd.windowHeight = height;

  initTextures(m_rd);
}

void Sample::end()
{
  nvgl::deleteBuffer(m_rd.buf.vbo);
  nvgl::deleteBuffer(m_rd.buf.ibo);
  nvgl::deleteBuffer(m_rd.buf.sceneUbo);
  nvgl::deleteBuffer(m_rd.buf.objectUbo);
  nvgl::deleteBuffer(m_rd.buf.composeUbo);

  nvgl::deleteTexture(m_rd.tex.colorTex);
  nvgl::deleteTexture(m_rd.tex.depthTex);

  m_rd.pm.deletePrograms();

  nvgl::deleteFramebuffer(m_rd.renderFBO);
}

int main(int argc, const char** argv)
{
  NVPSystem system(argv[0], PROJECT_NAME);

  Sample sample;
  return sample.run(PROJECT_NAME, argc, argv, SAMPLE_SIZE_WIDTH, SAMPLE_SIZE_HEIGHT);
}

void sample_print(int level, const char* fmt) {}
