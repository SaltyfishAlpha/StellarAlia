#version 450

layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragTangent;
layout(location = 4) in vec3 fragBitangent;

// G-Buffer outputs
layout(location = 0) out vec4 outPosition;      // RGB = position, A = unused
layout(location = 1) out vec4 outNormal;        // RGB = normal, A = unused
layout(location = 2) out vec4 outAlbedo;        // RGB = albedo, A = unused
layout(location = 3) out vec2 outMetallicRoughness; // R = metallic, G = roughness

layout(set = 1, binding = 0) uniform sampler2D texAlbedo;
layout(set = 1, binding = 1) uniform sampler2D texNormal;
layout(set = 1, binding = 2) uniform sampler2D texMetallicRoughness;
layout(set = 1, binding = 3) uniform sampler2D texAO;

layout(set = 1, binding = 4) uniform MaterialUniforms {
    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float aoStrength;
} material;

void main() {
    // Sample textures
    vec4 albedo = texture(texAlbedo, fragTexCoord) * material.baseColorFactor;
    vec3 normalMap = texture(texNormal, fragTexCoord).rgb;
    vec4 metallicRoughness = texture(texMetallicRoughness, fragTexCoord);
    float ao = texture(texAO, fragTexCoord).r;
    
    // Decode normal from normal map (tangent space to world space)
    vec3 N = normalize(fragNormal);
    vec3 T = normalize(fragTangent);
    vec3 B = normalize(fragBitangent);
    mat3 TBN = mat3(T, B, N);
    
    // Unpack normal from [0,1] to [-1,1]
    vec3 normalMapUnpacked = normalMap * 2.0 - 1.0;
    normalMapUnpacked.xy *= material.normalScale;
    N = normalize(TBN * normalMapUnpacked);
    
    // Extract metallic and roughness
    float metallic = metallicRoughness.b * material.metallicFactor;
    float roughness = metallicRoughness.g * material.roughnessFactor;
    
    // Write to G-Buffer
    // Position (store in view space or world space - using world space here)
    outPosition = vec4(fragPosition, 1.0);
    
    // Normal (encode from [-1,1] to [0,1])
    outNormal = vec4(N * 0.5 + 0.5, 1.0);
    
    // Albedo
    outAlbedo = vec4(albedo.rgb, 1.0);
    
    // Metallic and Roughness
    outMetallicRoughness = vec2(metallic, roughness);
    
    // Note: AO and emissive could be stored in additional attachments if needed
}

