#version 450
#extension GL_GOOGLE_include_directive : enable
#include "common.glsl"
#include "shading_models.glsl"     // assets/shaders/common/
#include "shading_model_ids.glsl"  // generated/shading_model_ids.glsl

// ── set=1 Material parameters ─────────────────────────────────────────────────
layout(set = 1, binding = 0) uniform MaterialParams {
    vec4  baseColorFactor;
    float roughnessFactor;
    float metallicFactor;
    float normalScale;
    float occlusionStrength;
    vec3  emissiveFactor;
    float _pad;
} u_Mat;

layout(set = 1, binding = 1) uniform sampler2D t_BaseColor;
layout(set = 1, binding = 2) uniform sampler2D t_Normal;
layout(set = 1, binding = 3) uniform sampler2D t_MetallicRoughness;  // g=roughness, b=metallic
layout(set = 1, binding = 4) uniform sampler2D t_Occlusion;
layout(set = 1, binding = 5) uniform sampler2D t_Emissive;

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
    vec4  albedo    = texture(t_BaseColor,         v_TexCoord0) * u_Mat.baseColorFactor;
    float occlusion = texture(t_Occlusion,         v_TexCoord0).r * u_Mat.occlusionStrength;
    vec2  mr        = texture(t_MetallicRoughness, v_TexCoord0).gb;  // g=roughness, b=metallic
    float roughness = clamp(mr.x * u_Mat.roughnessFactor, 0.04, 1.0);
    float metallic  = clamp(mr.y * u_Mat.metallicFactor,  0.0,  1.0);
    vec3  emissive  = texture(t_Emissive,          v_TexCoord0).rgb * u_Mat.emissiveFactor;

    // ── Normal (geometric only — no TBN, avoids normal-map texture artefacts) ──
    vec3 N = normalize(v_Normal);

    // ── Write G-Buffer ────────────────────────────────────────────────────────
    out_GAlbedoOcclusion = vec4(albedo.rgb, occlusion);
    out_GNormalRoughness  = vec4(OctEncode(N), roughness, metallic);
    out_GEmissiveMetallic = vec4(emissive, EncodeShadingFlags(SHADING_MODEL_PBR));
}
