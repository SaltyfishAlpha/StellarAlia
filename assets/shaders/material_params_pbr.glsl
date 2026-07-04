// ─────────────────────────────────────────────────────────────────────────────
// set=2 binding=0 MaterialParams (Issue #72: SSBO + bindless texture indices)
//
// Shared by deferred_geometry.frag / depth_prepass_mask.frag /
// forward_transparent.frag — all three consume the SAME per-draw blob
// (DrawItem.materialUboOffset), so this file is the single source of truth for
// the byte layout. Block name MUST be `MaterialParams` — MaterialManager
// detects it to enable the per-frame ring + BindlessTextureHeap path. Fields
// ending in `_Idx` (uint) are bindless heap indices into set=0 globalTex[].
// ─────────────────────────────────────────────────────────────────────────────
#ifndef SA_MATERIAL_PARAMS_PBR_GLSL
#define SA_MATERIAL_PARAMS_PBR_GLSL

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
    float alphaCutoff;              // @Range(0.0, 1.0, "Alpha Cutoff") = 0.5
} u_Mat;

// ── set=0 Global bindless texture heap (Issue #72) ───────────────────────────
layout(set = 0, binding = 0) uniform sampler2D globalTex[];

#endif // SA_MATERIAL_PARAMS_PBR_GLSL
