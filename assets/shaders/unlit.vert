#version 450
#extension GL_GOOGLE_include_directive : enable
#include "frame_uniforms.glsl"

layout(location = 0) in vec3 a_Position;
layout(location = 3) in vec2 a_TexCoord0;

layout(push_constant) uniform PushConstants { mat4 model; } pc;

layout(location = 0) out vec2 v_TexCoord0;

void main() {
    v_TexCoord0 = a_TexCoord0;
    gl_Position = u_Frame.viewProj * pc.model * vec4(a_Position, 1.0);
}
