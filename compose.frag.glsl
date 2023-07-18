#version 430 core

#extension GL_ARB_shading_language_include : enable
#include "common.h"

layout( binding = 0 ) uniform sampler2D tex;

layout(location = 0, index = 0) out vec4 Color;

void main()
{
  float out_x = gl_FragCoord.x;
  float out_y = gl_FragCoord.y;

  float in_x = compose.in_width  * out_x / compose.out_width;
  float in_y = compose.in_height * out_y / compose.out_height;

  Color = texelFetch( tex, ivec2( in_x, in_y ), 0 );
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


