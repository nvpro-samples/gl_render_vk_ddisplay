#version 430
/**/

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
  OUT.model_pos = vertex_pos_model;
}

/*-----------------------------------------------------------------------
  Copyright (c) 2019, NVIDIA. All rights reserved.
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

