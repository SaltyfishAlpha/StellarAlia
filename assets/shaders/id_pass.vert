#version 450
#extension GL_GOOGLE_include_directive : enable
#include "frame_uniforms.glsl"

// Editor ID picking pass (Issue #102). One draw per DrawItem; the item's
// 1-based index arrives via push constant and is routed flat to the fragment.
layout(location = 0) in vec3 a_Position;

layout(push_constant) uniform PC { mat4 model; uint id; } pc;

layout(location = 0) out flat uint v_id;

void main() {
    v_id = pc.id;
    gl_Position = u_Frame.viewProj * pc.model * vec4(a_Position, 1.0);
}
