#version 450
#extension GL_GOOGLE_include_directive : enable
#include "frame_uniforms.glsl"

layout(location = 0) in vec3 v_SkyDir;
layout(location = 0) out vec4 out_Color;

void main() {
    // Output raw HDR — tonemap handled by the separate TonemapPass.
    vec3 dir   = normalize(v_SkyDir);
    vec3 color = texture(t_SkyboxMap, dir).rgb;
    out_Color  = vec4(color, 1.0);
}
