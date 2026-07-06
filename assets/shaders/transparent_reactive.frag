// ─────────────────────────────────────────────────────────────────────────────
// Transparent reactive mask (Issue #105) — coverage-only companion to
// forward_transparent.frag. Redraws BLEND items into an R8 target; the
// (1,1,1,a) output under AlphaBlend accumulates the multi-layer coverage
// union dst' = a + dst·(1−a). TAA reads it to raise its blend weight on
// transparent pixels (they write no velocity — history would ghost).
// Alpha computation must mirror forward_transparent.frag's albedo sampling.
// Vertex shader = deferred_geometry(.vert|_skinned.vert).
// ─────────────────────────────────────────────────────────────────────────────
#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable
#include "material_params_pbr.glsl"  // set=2 MaterialParams + set=0 bindless heap

layout(location = 3) in vec2 v_TexCoord0;

layout(location = 0) out vec4 out_Coverage;

void main() {
    float a = texture(globalTex[nonuniformEXT(u_Mat.t_BaseColor_Idx)], v_TexCoord0).a
            * u_Mat.baseColorFactor.a;
    out_Coverage = vec4(1.0, 1.0, 1.0, a);
}
