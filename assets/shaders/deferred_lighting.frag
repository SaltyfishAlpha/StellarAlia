#version 450
#extension GL_GOOGLE_include_directive : enable

// set=0: frame uniforms, IBL textures, LTC LUTs (via pbr.glsl → frame_uniforms.glsl)
// set=1: G-Buffer inputs (filled by G-Buffer pass)
#include "pbr.glsl"
#include "shading_models.glsl"     // encode/decode helpers, SHADING_FLAG_HAS_RT3
#include "shading_model_ids.glsl"  // generated SHADING_MODEL_* defines

// ── G-Buffer inputs (set=1) ───────────────────────────────────────────────────
//   binding=0  RT0: albedo.rgb + occlusion.a                              (RGBA8_UNORM)
//   binding=1  RT1: octahedral-encoded normal (RG) + roughness(B) + metallic(A)  (RGBA16F)
//   binding=2  RT2: data.rgb + shading_model_id.a                         (RGBA16F)
//              RT2.rgb meaning depends on shading model (emissive.rgb for PBR).
//              RT2.a   = EncodeShadingFlags(modelID [, extraFlags])
//   binding=3  depth sampler (for world-pos reconstruction)
//   binding=4  shadow map (D32, directional light)
layout(set = 1, binding = 0) uniform sampler2D t_GAlbedoOcclusion;
layout(set = 1, binding = 1) uniform sampler2D t_GNormalMaterial;
layout(set = 1, binding = 2) uniform sampler2D t_GData;
layout(set = 1, binding = 3) uniform sampler2D t_GDepth;
layout(set = 1, binding = 4) uniform sampler2D t_ShadowMap;

// ── I/O ───────────────────────────────────────────────────────────────────────
layout(location = 0) in  vec2 v_TexCoord;
layout(location = 0) out vec4 out_HDRColor;

// ── G-Buffer data struct ──────────────────────────────────────────────────────
// Passed to all shading model evaluators (*.lighting.glsl).
// Fields are unpacked from the G-Buffer before dispatch; their meaning may vary
// per shading model — the G-Buffer fragment shader and its evaluator must agree.
struct GBufferData {
    vec3  albedo;
    float occlusion;
    vec3  N;
    float roughness;
    float metallic;
    vec3  data;      // RT2.rgb — emissive for PBR, custom for other models
    vec3  worldPos;
};

// ── Shading model evaluators (auto-generated dispatch) ───────────────────────
// GBufferData and shading_model_ids.glsl must be in scope before this include.
// Re-run CMake to regenerate when new *.lighting.glsl files are registered.
#include "shading_dispatch.glsl"

// ── PCF shadow (3×3, directional light only) ─────────────────────────────────
float ShadowFactor(vec3 worldPos, float NdotL) {
    vec4  lightClip    = u_Frame.lightSpaceMatrix * vec4(worldPos, 1.0);
    vec3  projCoords   = lightClip.xyz / lightClip.w;
    vec2  shadowUV     = projCoords.xy * 0.5 + 0.5;
    float currentDepth = projCoords.z;

    if (shadowUV.x <= 0.0 || shadowUV.x >= 1.0 ||
        shadowUV.y <= 0.0 || shadowUV.y >= 1.0 ||
        currentDepth < 0.0 || currentDepth > 1.0)
        return 1.0;

    float bias = u_Frame.shadowBias * max(1.0 - NdotL, 0.05);
    float shadow    = 0.0;
    vec2  texelSize = 1.0 / vec2(textureSize(t_ShadowMap, 0));
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y) {
            float closest = texture(t_ShadowMap,
                shadowUV + vec2(x, y) * texelSize).r;
            shadow += (currentDepth - bias > closest) ? 0.0 : 1.0;
        }
    return shadow / 9.0;
}

// ── Main ──────────────────────────────────────────────────────────────────────
void main() {
    // ── Sample G-Buffer ───────────────────────────────────────────────────────
    vec4  albedoOcc = texture(t_GAlbedoOcclusion, v_TexCoord);
    vec4  normalMat = texture(t_GNormalMaterial,  v_TexCoord);
    vec4  dataVec   = texture(t_GData,            v_TexCoord);
    float depth     = texture(t_GDepth,           v_TexCoord).r;

    // Background pixels (depth == 1.0): transparent output lets skybox show through.
    if (depth >= 1.0 - EPSILON) { out_HDRColor = vec4(0.0); return; }

    // ── Unpack into GBufferData ───────────────────────────────────────────────
    GBufferData gbuf;
    gbuf.albedo    = albedoOcc.rgb;
    gbuf.occlusion = albedoOcc.a;
    gbuf.N         = OctDecode(normalMat.rg);
    gbuf.roughness = normalMat.b;
    gbuf.metallic  = normalMat.a;
    gbuf.data      = dataVec.rgb;
    gbuf.worldPos  = ReconstructWorldPos(v_TexCoord, depth, u_Frame.invViewProj);

    // ── Dispatch to shading model ─────────────────────────────────────────────
    uint modelID = DecodeShadingModel(dataVec.a);

    vec3 customColor;
    if (DispatchShadingModel(modelID, gbuf, customColor)) {
        out_HDRColor = vec4(customColor, 1.0);
        return;
    }
    // DispatchShadingModel returns false for PBR (0) and unknown IDs → fall through

    // ── PBR (SHADING_MODEL_PBR = 0, and fallback for unknown IDs) ────────────
    vec3  albedo    = gbuf.albedo;
    float occlusion = gbuf.occlusion;
    vec3  N         = gbuf.N;
    float roughness = gbuf.roughness;
    float metallic  = gbuf.metallic;
    vec3  emissive  = gbuf.data;   // RT2.rgb = emissive for PBR
    vec3  worldPos  = gbuf.worldPos;

    vec3  V     = normalize(u_Frame.cameraPos - worldPos);
    vec3  R     = reflect(-V, N);
    float NdotV = max(dot(N, V), 0.0);
    vec3  F0    = mix(vec3(0.04), albedo, metallic);

    // ── Direct lighting loop ──────────────────────────────────────────────────
    vec3 direct = vec3(0.0);
    for (int i = 0; i < u_Lights.lightCount; ++i) {
        LightEntry light = u_Lights.lights[i];

        if (light.type == 3) {
            direct += EvaluateAreaLight(light, worldPos, N, V,
                                        roughness, F0, albedo, metallic);
            continue;
        }

        vec3  L;
        float attenuation = 1.0;

        if (light.type == 0) {
            L = normalize(-light.direction);
        } else {
            vec3  toLight = light.position - worldPos;
            float dist    = length(toLight);
            L = toLight / (dist + EPSILON);

            float t = clamp(dist / light.range, 0.0, 1.0);
            attenuation = clamp(1.0 - t * t * t * t, 0.0, 1.0)
                        / (dist * dist + 1.0);

            if (light.type == 2) {
                float cosAngle = dot(-L, normalize(light.direction));
                float cosInner = cos(light.innerAngle);
                float cosOuter = cos(light.outerAngle);
                attenuation *= clamp(
                    (cosAngle - cosOuter) / (cosInner - cosOuter + EPSILON),
                    0.0, 1.0);
            }
        }

        float NdotL_shadow = max(dot(N, L), 0.0);
        float shadowFactor = (light.type == 0) ? ShadowFactor(worldPos, NdotL_shadow) : 1.0;

        vec3  H     = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);
        float NdotH = max(dot(N, H), 0.0);
        float HdotV = max(dot(H, V), 0.0);

        float NDF = DistributionGGX(NdotH, roughness);
        float G   = GeometrySmith(NdotV, NdotL, roughness);
        vec3  F   = FresnelSchlick(HdotV, F0);

        vec3 kD       = (vec3(1.0) - F) * (1.0 - metallic);
        vec3 specBRDF = NDF * G * F / (4.0 * NdotV * NdotL + EPSILON);
        direct += (kD * albedo * INV_PI + specBRDF)
                * light.color * light.intensity * NdotL * attenuation * shadowFactor;
    }

    // ── IBL diffuse (SH) ──────────────────────────────────────────────────────
    vec3 F_ibl      = FresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kD_ibl     = (1.0 - F_ibl) * (1.0 - metallic);
    vec3 irradiance = EvaluateSHIrradiance(N);
    vec3 iblDiffuse = kD_ibl * irradiance * albedo * occlusion;

    // ── IBL specular (split-sum) ──────────────────────────────────────────────
    const float MAX_REFLECTION_LOD = 4.0;
    vec3 prefilteredColor = textureLod(t_PrefilteredEnv, R,
                                       roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdfSS      = texture(t_BrdfLut, vec2(NdotV, roughness)).rg;
    vec3 iblSpecular = prefilteredColor * (F_ibl * brdfSS.x + brdfSS.y) * occlusion;

    vec3 color = direct + iblDiffuse + iblSpecular + emissive;
    out_HDRColor = vec4(color, 1.0);
}
