#version 430
/**/

#extension GL_ARB_shading_language_include : enable
#include "common.h"
#include "noise.glsl"

// inputs in view space
in Interpolants {
  vec3 model_pos;
  vec3 normal;
  vec3 eyeDir;
  vec3 lightDir;
} IN;

layout(location=0,index=0) out vec4 out_Color;

void main()
{
  // interpolated inputs in view space
  vec3 normal   = normalize(IN.normal);
  vec3 eyeDir   = normalize(IN.eyeDir);
  vec3 lightDir = normalize(IN.lightDir);

  // simulate a heavy fragment shader with this loop
  int load = scene.fragmentLoad * 42;
  float val = 0; 
  if( load > 0 )
  {
    for( int i = 0; i < load; ++i )
    {
      val += (SimplexPerlin3D(IN.model_pos*4)) / load;
    }
    val = smoothstep( -0.1, 0.2, val );
  }

  vec3 objectColor = object.color + vec3(0,val,0);

  // ambient term
  vec4 ambient_color = vec4( objectColor * scene.backgroundColor * 0.15, 1.0 );

  // diffuse term
  float diffuse_intensity = max( dot(normal,lightDir), 0.0 );
  vec4  diffuse_color = diffuse_intensity * vec4(objectColor, 1.0);

  // specular term
  vec3  R = reflect( -lightDir, normal );
  float specular_intensity = max( dot( eyeDir, R ), 0.0 );
  vec4  specular_color = pow(specular_intensity, 4) * vec4(0.8,0.8,0.8,1);

  out_Color = ambient_color + diffuse_color + specular_color;
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

