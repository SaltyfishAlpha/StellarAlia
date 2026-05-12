#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable
#include "common.glsl"
#include "shading_models.glsl"     // assets/shaders/common/
#include "shading_model_ids.glsl"  // generated/shading_model_ids.glsl

// ── set=2 Material parameters (Issue #72: SSBO + bindless texture indices) ───
// Block name MUST be `MaterialParams` — MaterialManager detects it to enable the
// per-frame ring + BindlessTextureHeap path. Fields ending in `_Idx` (uint) are
// bindless heap indices into set=0 globalTex[].
layout(std430, set = 2, binding = 0) readonly buffer MaterialParams {
    vec4  baseColorFactor;          // @Color4("Base Color") = 1,1,1,1
    float roughnessFactor;          // @Range(0.0, 1.0, "Roughness") = 0.5
    float metallicFactor;           // @Range(0.0, 1.0, "Metallic") = 0.0
    float normalScale;              // @Float("Normal Scale") = 1.0
    float occlusionStrength;        // @Range(0.0, 1.0, "Occlusion Strength") = 1.0
    vec3  emissiveFactor;           // @Color3("Emissive Color") = 0,0,0
    float emissiveIntensity;        // @Range(0.0, 50.0, "Emissive Intensity") = 1.0
    uint  t_BaseColor_Idx;          // @Texture("Albedo Map")
    uint  t_Normal_Idx;             // @Texture("Normal Map")
    uint  t_MetallicRoughness_Idx;  // @Texture("Metallic Roughness")
    uint  t_Occlusion_Idx;          // @Texture("Occlusion Map")
    uint  t_Emissive_Idx;           // @Texture("Emissive Map")
} u_Mat;

// ── set=0 Global bindless texture heap (Issue #72) ───────────────────────────
layout(set = 0, binding = 0) uniform sampler2D globalTex[];

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
