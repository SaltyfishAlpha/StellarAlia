// ─────────────────────────────────────────────────────────────────────────────
// Forward Transparent (Issue #56) — BLEND materials, drawn back-to-front after
// TAA onto the copied resolve target (see forward_copy.frag).
//
// Material sampling mirrors deferred_geometry.frag (same MaterialParams SSBO
// blob via DrawItem.materialUboOffset); shading is the shared PBR path from
// pbr_shading.glsl. PBR only — custom .saglsl shading models are deferred-
// dispatch and legacy-UBO, they cannot take this path.
// Vertex shader = deferred_geometry(.vert|_skinned.vert).
// ─────────────────────────────────────────────────────────────────────────────
#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable
#include "pbr.glsl"                  // BRDF + u_Frame/u_Lights + IBL + t_ShadowMap (set=1)
#include "material_params_pbr.glsl"  // set=2 MaterialParams + set=0 bindless heap
#include "pbr_shading.glsl"          // EvaluatePBRShading / ShadowFactorPCF

layout(location = 0) in vec3 v_WorldPos;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec4 v_Tangent;
layout(location = 3) in vec2 v_TexCoord0;

layout(location = 0) out vec4 out_HDRColor;

void main() {
    vec4  albedo    = texture(globalTex[nonuniformEXT(u_Mat.t_BaseColor_Idx)],         v_TexCoord0) * u_Mat.baseColorFactor;
    float occlusion = texture(globalTex[nonuniformEXT(u_Mat.t_Occlusion_Idx)],         v_TexCoord0).r * u_Mat.occlusionStrength;
    vec2  mr        = texture(globalTex[nonuniformEXT(u_Mat.t_MetallicRoughness_Idx)], v_TexCoord0).gb;
    float roughness = clamp(mr.x * u_Mat.roughnessFactor, 0.04, 1.0);
    float metallic  = clamp(mr.y * u_Mat.metallicFactor,  0.0,  1.0);
    vec3  emissive  = texture(globalTex[nonuniformEXT(u_Mat.t_Emissive_Idx)],          v_TexCoord0).rgb * u_Mat.emissiveFactor * u_Mat.emissiveIntensity;

    vec3 N = normalize(v_Normal);
    if (!gl_FrontFacing) N = -N;   // double-sided glass: shade the visible face

    vec3 color = EvaluatePBRShading(t_ShadowMap,
                                    albedo.rgb, occlusion, N,
                                    roughness, metallic, emissive, v_WorldPos);
    out_HDRColor = vec4(color, albedo.a);
}
