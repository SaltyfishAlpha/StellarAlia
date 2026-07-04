// ─────────────────────────────────────────────────────────────────────────────
// Shared PBR shading (Issue #56) — direct light loop + PCF shadow + IBL SH
// diffuse + split-sum specular. Consumed by deferred_lighting.frag (deferred
// fallback path) and forward_transparent.frag (forward translucency).
//
// Requires pbr.glsl (BRDF helpers, u_Frame/u_Lights, IBL samplers, LTC area
// lights) to be included first. The shadow map is passed as a parameter —
// deferred binds it at set=2 binding=4, forward at set=1 binding=7.
//
// NOTE: deferred_lighting.frag is recompiled AT RUNTIME by ShaderCook
// (project .saglsl dispatch); this file must stay in assets/shaders/
// (ENGINE_SHADER_SRC_DIR) so both compile paths resolve it.
// ─────────────────────────────────────────────────────────────────────────────
#ifndef SA_PBR_SHADING_GLSL
#define SA_PBR_SHADING_GLSL

// PCF shadow (3×3, directional light only).
float ShadowFactorPCF(sampler2D shadowMap, vec3 worldPos, float NdotL) {
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
    vec2  texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y) {
            float closest = texture(shadowMap,
                shadowUV + vec2(x, y) * texelSize).r;
            shadow += (currentDepth - bias > closest) ? 0.0 : 1.0;
        }
    return shadow / 9.0;
}

vec3 EvaluatePBRShading(sampler2D shadowMap,
                        vec3  albedo,    float occlusion,
                        vec3  N,         float roughness,
                        float metallic,  vec3  emissive,
                        vec3  worldPos) {
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
        float shadowFactor = (light.type == 0)
                                 ? ShadowFactorPCF(shadowMap, worldPos, NdotL_shadow)
                                 : 1.0;

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

    return direct + iblDiffuse + iblSpecular + emissive;
}

#endif // SA_PBR_SHADING_GLSL
