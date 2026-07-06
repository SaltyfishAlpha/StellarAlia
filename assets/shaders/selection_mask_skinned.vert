#version 450
#extension GL_GOOGLE_include_directive : enable
#include "frame_uniforms.glsl"
#include "skin_deform.glsl"

layout(location = 0) in vec3 a_Position;

layout(push_constant) uniform PC { mat4 model; } pc;

void main() {
    // Unjittered (Issue #107) — see selection_mask.vert.
    gl_Position = u_Frame.currUnjitteredViewProj * pc.model * SkinMatrix() * vec4(a_Position, 1.0);
}
