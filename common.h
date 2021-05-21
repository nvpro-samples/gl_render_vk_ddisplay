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


