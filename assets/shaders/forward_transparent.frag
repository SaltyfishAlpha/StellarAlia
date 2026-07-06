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
#include "pbr.glsl"                  // BRDF + u_Frame/u_Lights + IBL + t_ShadowMap/t_FogVolume (set=1)
#include "material_params_pbr.glsl"  // set=2 MaterialParams + set=0 bindless heap
#include "pbr_shading.glsl"          // EvaluatePBRShading / ShadowFactorPCF
#include "volumetric_common.glsl"    // VolFogDepthToSlice (Issue #49 Step 9)

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

    // Issue #49 Step 9: fog to the fragment's own depth — camera-to-surface
    // transmittance + inscatter, sampled per fragment from the froxel volume.
    // Fog off binds a (0,0,0,1) dummy → no-op. AlphaBlend then gives
    // a·(surface·T + inscatter) + (1−a)·background (already fully fogged).
    {
        vec4 vpos  = u_Frame.invProj * vec4(0.0, 0.0, gl_FragCoord.z, 1.0);
        float viewD = -vpos.z / vpos.w;
        vec3 volSize = vec3(textureSize(t_FogVolume, 0));
        float slice  = VolFogDepthToSlice(min(viewD, u_Frame.volFogFar),
                                          volSize.z, u_Frame.volFogFar);
        // Same half-voxel shift + half-texel clamps as volumetric_apply.frag
        // (voxel k = integral to slice-k END; repeat sampler must not wrap).
        float wCoord = clamp((slice - 0.5) / volSize.z,
                             0.5 / volSize.z, 1.0 - 0.5 / volSize.z);
        vec2 fogUV = clamp(gl_FragCoord.xy / u_Frame.resolution,
                           0.5 / volSize.xy, 1.0 - 0.5 / volSize.xy);
        vec4 fog = texture(t_FogVolume, vec3(fogUV, wCoord));
        color = color * fog.a + fog.rgb;
    }

    out_HDRColor = vec4(color, albedo.a);
}
