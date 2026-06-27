#version 450
#extension GL_GOOGLE_include_directive : enable
#include "frame_uniforms.glsl"

// Static-mesh velocity prepass (Issue #84).
// Reuses standard vertex layout (location 0 = position). Other inputs unused
// — kept to share VkPipelineVertexInputState with the GBuffer pipeline.
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec4 a_Tangent;
layout(location = 3) in vec2 a_TexCoord0;

layout(push_constant) uniform PC {
    mat4 currModel;
    mat4 prevModel;
} pc;

layout(location = 0) out vec4 v_CurrClip;
layout(location = 1) out vec4 v_PrevClip;

void main() {
    vec4 currWorld = pc.currModel * vec4(a_Position, 1.0);
    vec4 prevWorld = pc.prevModel * vec4(a_Position, 1.0);

    // Issue #85: rasterize with jittered viewProj (matches GBuffer depth),
    // but write velocity from UNJITTERED currVP so downstream consumers (TAA /
    // MotionBlur / future SSR) get jitter-free per-pixel motion vectors.
    gl_Position  = u_Frame.viewProj               * currWorld;   // jittered
    v_CurrClip   = u_Frame.currUnjitteredViewProj * currWorld;   // unjittered
    v_PrevClip   = u_Frame.prevViewProj           * prevWorld;   // unjittered
}
