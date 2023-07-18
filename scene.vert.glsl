 #version 430

#extension GL_ARB_shading_language_include : enable
#include "common.h"

// inputs in model space
in layout(location=VERTEX_POS)    vec3 vertex_pos_model;
in layout(location=VERTEX_NORMAL) vec3 normal;

// outputs in view space
out Interpolants {
  vec3 model_pos;
  vec3 normal;
  vec3 eyeDir;
  vec3 lightDir;
} OUT;

void main()
{
  // proj space calculations
  gl_Position   = object.modelViewProj * vec4( vertex_pos_model, 1 );

  // view space calculations
  vec3 pos      = (object.modelView   * vec4(vertex_pos_model,1)).xyz;
  vec3 lightPos = (scene.viewMatrix   * vec4(scene.lightPos_world,1)).xyz;
  OUT.normal    = (object.modelViewIT * vec4(normal,0)).xyz;
  OUT.eyeDir    = scene.eyePos_view - pos;
  OUT.lightDir  = lightPos - pos;
  OUT.model_pos = vertex_pos_model+pos;
}

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


