#version 450

#include "pbr.glsl"

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

// G-Buffer inputs
layout(set = 0, binding = 0) uniform sampler2D gPosition;
layout(set = 0, binding = 1) uniform sampler2D gNormal;
layout(set = 0, binding = 2) uniform sampler2D gAlbedo;
layout(set = 0, binding = 3) uniform sampler2D gMetallicRoughness;

// Lighting uniform
layout(set = 0, binding = 4) uniform LightingUniforms {
    vec3 lightPosition;
    vec3 lightColor;
    float lightIntensity;
    float lightRadius;
    vec3 viewPosition;
    vec3 ambientColor;
    float ambientIntensity;
} lighting;

void main() {
    // Sample G-Buffer
    vec3 position = texture(gPosition, fragTexCoord).rgb;
    vec3 normal = texture(gNormal, fragTexCoord).rgb * 2.0 - 1.0; // Decode from [0,1] to [-1,1]
    vec3 albedo = texture(gAlbedo, fragTexCoord).rgb;
    vec2 metallicRoughness = texture(gMetallicRoughness, fragTexCoord).rg;
    
    float metallic = metallicRoughness.r;
    float roughness = metallicRoughness.g;
    
    // View direction
    vec3 V = normalize(lighting.viewPosition - position);
    vec3 N = normalize(normal);
    
    // Light calculation
    vec3 L = normalize(lighting.lightPosition - position);
    float distance = length(lighting.lightPosition - position);
    
    // Attenuation (simple inverse square with radius cutoff)
    float attenuation = 1.0 / (1.0 + 0.09 * distance + 0.032 * distance * distance);
    attenuation *= smoothstep(lighting.lightRadius, lighting.lightRadius * 0.5, distance);
    
    // Calculate lighting
    vec3 radiance = lighting.lightColor * lighting.lightIntensity * attenuation;
    
    // PBR lighting
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);
    
    // Direct lighting (Cook-Torrance BRDF)
    vec3 H = normalize(V + L);
    float NDF = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;
    
    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + EPSILON;
    vec3 specular = numerator / max(denominator, EPSILON);
    
    float NdotL = max(dot(N, L), 0.0);
    vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;
    
    // Ambient lighting
    F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    kS = F;
    kD = 1.0 - kS;
    kD *= 1.0 - metallic;
    vec3 ambient = lighting.ambientColor * lighting.ambientIntensity * albedo * kD;
    
    // Final color
    vec3 color = ambient + Lo;
    
    // Tone mapping (simple Reinhard)
    color = color / (color + vec3(1.0));
    
    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));
    
    outColor = vec4(color, 1.0);
}

