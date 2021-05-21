#version 430 core

#extension GL_ARB_shading_language_include : enable
#include "common.h"

// draw a triangle that covers the whole screen
const vec4 pos[3] = vec4[3]( vec4( -1, -1, 0, 1 )
                           , vec4(  3, -1, 0, 1 )
                           , vec4( -1,  3, 0, 1 ) );

void main()
{
  gl_Position = pos[ gl_VertexID ];
}


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


