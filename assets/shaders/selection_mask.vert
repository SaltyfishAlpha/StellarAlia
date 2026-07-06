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
    // Unjittered (Issue #107): the outline is composited after TAA, which
    // converges to the unjittered position — a jittered mask wobbles ±0.5px.
    gl_Position = u_Frame.currUnjitteredViewProj * pc.model * vec4(a_Position, 1.0);
}
