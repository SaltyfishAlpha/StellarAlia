#version 450
#extension GL_GOOGLE_include_directive : enable
#include "frame_uniforms.glsl"

// Minimal vertex shader for the selection mask pass.
// Same vertex format as the main geometry pass (48 bytes / vertex).
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;     // unused; present to satisfy vertex binding
layout(location = 2) in vec4 a_Tangent;    // unused
layout(location = 3) in vec2 a_TexCoord0;  // unused

layout(push_constant) uniform PC { mat4 model; } pc;

void main() {
    gl_Position = u_Frame.viewProj * pc.model * vec4(a_Position, 1.0);
}
