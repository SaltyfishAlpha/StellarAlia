#version 450
#extension GL_GOOGLE_include_directive : enable
#include "frame_uniforms.glsl"

// Minimal vertex shader for the selection mask pass.
// Pipeline vertex input is reflection-driven, so only the consumed location is
// declared here — the mesh buffer is still 48-byte interleaved, but the
// pipeline emits a single attrib at location 0.
layout(location = 0) in vec3 a_Position;

layout(push_constant) uniform PC { mat4 model; } pc;

void main() {
    gl_Position = u_Frame.viewProj * pc.model * vec4(a_Position, 1.0);
}
