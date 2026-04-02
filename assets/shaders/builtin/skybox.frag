#version 450
#extension GL_GOOGLE_include_directive : enable
#include "frame_uniforms.glsl"

layout(location = 0) in vec3 v_SkyDir;
layout(location = 0) out vec4 out_Color;

const float PI = 3.14159265359;

vec2 DirToEquirect(vec3 dir) {
    float phi   = atan(dir.z, dir.x);
    float theta = asin(clamp(dir.y, -1.0, 1.0));
    return vec2(phi / (2.0 * PI) + 0.5, -theta / PI + 0.5);
}

void main() {
    vec3 dir   = normalize(v_SkyDir);
    vec3 color = textureLod(t_SkyboxMap, DirToEquirect(dir), 0.0).rgb;

    // ACES filmic tone mapping
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    color = clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);

    out_Color = vec4(color, 1.0);
}
