#version 450
#extension GL_GOOGLE_include_directive : enable
#include "frame_uniforms.glsl"

// ── set=1 Material parameters (MaterialInstance uploads these) ────────────────
layout(set = 1, binding = 0) uniform MaterialParams {
    vec4  baseColorFactor;   // linear RGBA, multiplied with texture
    float roughnessFactor;
    float metallicFactor;
    float normalScale;
    float occlusionStrength;
    vec3  emissiveFactor;
    float _pad;
} u_Mat;

layout(set = 1, binding = 1) uniform sampler2D t_BaseColor;
layout(set = 1, binding = 2) uniform sampler2D t_Normal;
layout(set = 1, binding = 3) uniform sampler2D t_MetallicRoughness; // g=roughness b=metallic
layout(set = 1, binding = 4) uniform sampler2D t_Occlusion;
layout(set = 1, binding = 5) uniform sampler2D t_Emissive;

// ── Inputs from vertex stage ──────────────────────────────────────────────────
layout(location = 0) in vec3 v_WorldPos;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec4 v_Tangent;
layout(location = 3) in vec2 v_TexCoord0;

layout(location = 0) out vec4 out_Color;

// ── Stub implementation (full PBR BRDF deferred to Stage 4) ──────────────────
void main() {
    vec4  albedo    = texture(t_BaseColor, v_TexCoord0) * u_Mat.baseColorFactor;
    vec3  N         = normalize(v_Normal);
    float NdotL     = max(dot(N, -normalize(u_Light.direction)), 0.0);
    vec3  diffuse   = albedo.rgb * u_Light.color * u_Light.intensity * NdotL;
    vec3  ambient   = albedo.rgb * u_Light.ambientColor * u_Light.ambientIntensity;
    out_Color = vec4(diffuse + ambient, albedo.a);
}
