#version 450
#extension GL_GOOGLE_include_directive : enable
#include "frame_uniforms.glsl"
#include "skin_deform_dual.glsl"

// Skinned-mesh velocity prepass (Issue #84).
// Samples curr + prev bone matrices to compute per-vertex velocity that
// captures both rigid-body motion (currModel vs prevModel) and pose
// deformation (currSkin vs prevSkin).
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
    mat4 currSkin = SkinMatrix();
    mat4 prevSkin = SkinMatrixPrev();

    vec4 currWorld = pc.currModel * currSkin * vec4(a_Position, 1.0);
    vec4 prevWorld = pc.prevModel * prevSkin * vec4(a_Position, 1.0);

    // Issue #85: jittered for rasterization, unjittered for velocity.
    gl_Position  = u_Frame.viewProj               * currWorld;
    v_CurrClip   = u_Frame.currUnjitteredViewProj * currWorld;
    v_PrevClip   = u_Frame.prevViewProj           * prevWorld;
}
