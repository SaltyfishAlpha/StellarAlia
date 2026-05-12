#version 450
#extension GL_GOOGLE_include_directive : enable

// All PBR/LTC/SH functions and frame uniforms live in shared headers.
// This file only contains material inputs, vertex I/O, and main().
#include "pbr.glsl"

// ── set=1 Material parameters ─────────────────────────────────────────────────
layout(set = 2, binding = 0) uniform MaterialParams {
    vec4  baseColorFactor;   // @Color4("Base Color") = 1,1,1,1
    float roughnessFactor;   // @Range(0.0, 1.0, "Roughness") = 0.5
    float metallicFactor;    // @Range(0.0, 1.0, "Metallic") = 0.0
    float normalScale;       // @Float("Normal Scale") = 1.0
    float occlusionStrength; // @Range(0.0, 1.0, "Occlusion Strength") = 1.0
    vec3  emissiveFactor;       // @Color3("Emissive Color") = 0,0,0
    float emissiveIntensity;    // @Range(0.0, 50.0, "Emissive Intensity") = 1.0
} u_Mat;

layout(set = 2, binding = 1) uniform sampler2D t_BaseColor;         // @Texture("Albedo Map")
layout(set = 2, binding = 2) uniform sampler2D t_Normal;             // @Texture("Normal Map")
layout(set = 2, binding = 3) uniform sampler2D t_MetallicRoughness; // @Texture("Metallic Roughness")
layout(set = 2, binding = 4) uniform sampler2D t_Occlusion;         // @Texture("Occlusion Map")
layout(set = 2, binding = 5) uniform sampler2D t_Emissive;          // @Texture("Emissive Map")

// ── Inputs from vertex stage ──────────────────────────────────────────────────
layout(location = 0) in vec3 v_WorldPos;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec4 v_Tangent;
layout(location = 3) in vec2 v_TexCoord0;

layout(location = 0) out vec4 out_Color;

// ── Main ──────────────────────────────────────────────────────────────────────
void main() {
    // Material inputs
    vec4  albedo    = texture(t_BaseColor,          v_TexCoord0) * u_Mat.baseColorFactor;
    float occlusion = texture(t_Occlusion,          v_TexCoord0).r * u_Mat.occlusionStrength;
    vec2  mr        = texture(t_MetallicRoughness,  v_TexCoord0).gb; // g=rough, b=metal
    float roughness = clamp(mr.x * u_Mat.roughnessFactor, 0.04, 1.0);
    float metallic  = clamp(mr.y * u_Mat.metallicFactor,  0.0,  1.0);
    vec3  emissive  = texture(t_Emissive,           v_TexCoord0).rgb * u_Mat.emissiveFactor * u_Mat.emissiveIntensity;

    vec3  N     = normalize(v_Normal);
    vec3  V     = normalize(u_Frame.cameraPos - v_WorldPos);
    vec3  R     = reflect(-V, N);
    float NdotV = max(dot(N, V), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo.rgb, metallic);

    // ── Direct lighting loop (directional / point / spot / area) ────────────
    vec3 direct = vec3(0.0);
    for (int i = 0; i < u_Lights.lightCount; ++i) {
        LightEntry light = u_Lights.lights[i];

        if (light.type == 3) {
            // Area (rectangle) — LTC evaluation
            direct += EvaluateAreaLight(light, v_WorldPos, N, V,
                                        roughness, F0, albedo.rgb, metallic);
            continue;
        }

        vec3  L;
        float attenuation = 1.0;

        if (light.type == 0) {
            // Directional — no falloff, direction is world-space forward
            L = normalize(-light.direction);
        } else {
            // Point and spot — L points from surface toward the light
            vec3  toLight = light.position - v_WorldPos;
            float dist    = length(toLight);
            L = toLight / (dist + 1e-7);

            // Smooth inverse-square falloff, clamped to [0,range]
            float t = clamp(dist / light.range, 0.0, 1.0);
            attenuation = clamp(1.0 - t * t * t * t, 0.0, 1.0)
                        / (dist * dist + 1.0);

            if (light.type == 2) {
                // Spot — additional angular cone attenuation
                float cosAngle = dot(-L, normalize(light.direction));
                float cosInner = cos(light.innerAngle);
                float cosOuter = cos(light.outerAngle);
                attenuation *= clamp(
                    (cosAngle - cosOuter) / (cosInner - cosOuter + 1e-7),
                    0.0, 1.0);
            }
        }

        vec3  H     = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);
        float NdotH = max(dot(N, H), 0.0);
        float HdotV = max(dot(H, V), 0.0);

        float NDF = DistributionGGX(NdotH, roughness);
        float G   = GeometrySmith(NdotV, NdotL, roughness);
        vec3  F   = FresnelSchlick(HdotV, F0);

        vec3 kD       = (vec3(1.0) - F) * (1.0 - metallic);
        vec3 specBRDF = NDF * G * F / (4.0 * NdotV * NdotL + 1e-7);
        direct += (kD * albedo.rgb / PI + specBRDF)
                  * light.color * light.intensity * NdotL * attenuation;
    }

    // ── IBL diffuse irradiance (SH) ───────────────────────────────────────────
    vec3 F_ibl    = FresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kD_ibl   = (1.0 - F_ibl) * (1.0 - metallic);
    vec3 irradiance = EvaluateSHIrradiance(N);
    vec3 iblDiffuse = kD_ibl * irradiance * albedo.rgb * occlusion;

    // ── IBL specular (split-sum) ───────────────────────────────────────────────
    const float MAX_REFLECTION_LOD = 4.0;
    vec3  prefilteredColor = textureLod(t_PrefilteredEnv,
                                        R,
                                        roughness * MAX_REFLECTION_LOD).rgb;
    vec2  brdfSS    = texture(t_BrdfLut, vec2(NdotV, roughness)).rg;
    vec3  iblSpecular = prefilteredColor * (F_ibl * brdfSS.x + brdfSS.y) * occlusion;

    vec3 color = direct + iblDiffuse + iblSpecular + emissive;
    out_Color = vec4(color, albedo.a);
}
