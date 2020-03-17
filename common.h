#pragma once

// scene data defines
#define VERTEX_POS        0
#define VERTEX_NORMAL     1
#define VERTEX_TEX        2

#define UBO_SCENE         0
#define UBO_OBJECT        1

// compose data defines
#define UBO_COMP          0

#ifdef __cplusplus
using namespace nvmath;
#endif

struct SceneData
{
  mat4 viewMatrix;      // view matrix: world->view
  mat4 projMatrix;      // proj matrix: view ->proj
  mat4 viewProjMatrix;  // viewproj   : world->proj
  vec3 lightPos_world;  // light position in world space
  vec3 eyepos_world;    // eye position in world space
  vec3 eyePos_view;     // eye position in view space
  vec3 backgroundColor; // scene background color

  int  fragmentLoad;

  float projNear
#ifdef __cplusplus
    = 0.01f
#endif
    ;
  float projFar
#ifdef __cplusplus
    = 100.0f
#endif
    ;
};

struct ObjectData
{
  mat4 model;         // model -> world
  mat4 modelView;     // model -> view
  mat4 modelViewIT;   // model -> view for normals
  mat4 modelViewProj; // model -> proj
  vec3 color;         // object color
};

struct ComposeData
{
  int in_width;     // width of the input textures
  int in_height;    // height of the input textures
  int out_width;    // width of the output buffer
  int out_height;   // height of the output buffer
};

#if defined(GL_core_profile) || defined(GL_compatibility_profile) || defined(GL_es_profile)
// prevent this to be used by c++

#if defined(USE_SCENE_DATA)
layout(std140, binding = UBO_SCENE) uniform sceneBuffer {
  SceneData scene;
};
layout(std140, binding = UBO_OBJECT) uniform objectBuffer {
  ObjectData object;
};
#endif

#if defined(USE_COMPOSE_DATA)
layout(std140, binding = UBO_COMP) uniform composeBuffer {
  ComposeData compose;
};
#endif

#endif


/*-----------------------------------------------------------------------
Copyright (c) 2014, NVIDIA. All rights reserved.
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
* Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
* Neither the name of its contributors may be used to endorse
or promote products derived from this software without specific
prior written permission.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
-----------------------------------------------------------------------*/

