#version 450
#extension GL_GOOGLE_include_directive : enable
#include "frame_uniforms.glsl"

// Depth-only pass — only a_Position is consumed; other attributes exist in the
// 48-byte interleaved buffer but are not declared here.
layout(location = 0) in vec3 a_Position;

layout(push_constant) uniform PushConstants {
    mat4 model;
} pc;

void main() {
    gl_Position = u_Frame.lightSpaceMatrix * pc.model * vec4(a_Position, 1.0);
}
