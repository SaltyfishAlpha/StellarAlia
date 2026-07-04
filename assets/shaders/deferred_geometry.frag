#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable
#include "common.glsl"
#include "shading_models.glsl"     // assets/shaders/common/
#include "shading_model_ids.glsl"  // generated/shading_model_ids.glsl

// Issue #56: no discard in this shader — force early-Z so the MASK variant
// (depthCompareOp=EQUAL, prepass-filled depth) rejects occluded/cut-out texels
// before this fragment runs.
layout(early_fragment_tests) in;

// ── set=2 MaterialParams + set=0 bindless heap (shared, Issue #56) ───────────
#include "material_params_pbr.glsl"

// ── Inputs from vertex stage ──────────────────────────────────────────────────
layout(location = 0) in vec3 v_WorldPos;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec4 v_Tangent;
layout(location = 3) in vec2 v_TexCoord0;

// ── G-Buffer outputs ──────────────────────────────────────────────────────────
//   RT0: albedo.rgb + occlusion.a                          (RGBA8_UNORM)
//   RT1: octahedral-encoded normal (RG) + roughness(B) + metallic(A)  (RGBA16F)
//   RT2: emissive.rgb                                      (RGBA16F)
layout(location = 0) out vec4 out_GAlbedoOcclusion;
layout(location = 1) out vec4 out_GNormalRoughness;
layout(location = 2) out vec4 out_GEmissiveMetallic;

void main() {
    // ── Material inputs ───────────────────────────────────────────────────────
    vec4  albedo    = texture(globalTex[nonuniformEXT(u_Mat.t_BaseColor_Idx)],         v_TexCoord0) * u_Mat.baseColorFactor;
    float occlusion = texture(globalTex[nonuniformEXT(u_Mat.t_Occlusion_Idx)],         v_TexCoord0).r * u_Mat.occlusionStrength;
    vec2  mr        = texture(globalTex[nonuniformEXT(u_Mat.t_MetallicRoughness_Idx)], v_TexCoord0).gb;
    float roughness = clamp(mr.x * u_Mat.roughnessFactor, 0.04, 1.0);
    float metallic  = clamp(mr.y * u_Mat.metallicFactor,  0.0,  1.0);
    vec3  emissive  = texture(globalTex[nonuniformEXT(u_Mat.t_Emissive_Idx)],          v_TexCoord0).rgb * u_Mat.emissiveFactor * u_Mat.emissiveIntensity;

    // ── Normal (geometric only — no TBN, avoids normal-map texture artefacts) ──
    vec3 N = normalize(v_Normal);

    // ── Write G-Buffer ────────────────────────────────────────────────────────
    out_GAlbedoOcclusion = vec4(albedo.rgb, occlusion);
    out_GNormalRoughness  = vec4(OctEncode(N), roughness, metallic);
    out_GEmissiveMetallic = vec4(emissive, EncodeShadingFlags(SHADING_MODEL_PBR));
}
