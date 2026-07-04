// ─────────────────────────────────────────────────────────────────────────────
// Depth Prepass — MASK (alpha-test) geometry only (Issue #56).
//
// The ONLY place `discard` runs for masked materials: one albedo.a sample,
// depth + stencil(=1) written by fixed function. The GBuffer main pass then
// rasterises the same geometry with depthCompareOp=EQUAL / depthWrite=off, so
// its expensive PBR fragment work is early-Z-rejected for occluded/cut-out
// texels. Vertex shader = deferred_geometry(.vert|_skinned.vert) — reusing it
// keeps prepass and GBuffer depth bit-exact (same jittered VP path).
// ─────────────────────────────────────────────────────────────────────────────
#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable
#include "material_params_pbr.glsl"

layout(location = 3) in vec2 v_TexCoord0;

void main() {
    const float a = texture(globalTex[nonuniformEXT(u_Mat.t_BaseColor_Idx)],
                            v_TexCoord0).a
                  * u_Mat.baseColorFactor.a;
    if (a < u_Mat.alphaCutoff) discard;
}
