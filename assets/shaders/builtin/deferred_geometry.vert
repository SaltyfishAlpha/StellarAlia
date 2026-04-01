#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;

layout(location = 0) out vec3 fragPosition;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragTangent;
layout(location = 4) out vec3 fragBitangent;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 normalMatrix;
} ubo;

void main() {
    // Transform position to world space
    vec4 worldPos = ubo.model * vec4(inPosition, 1.0);
    fragPosition = worldPos.xyz;
    
    // Transform normal to world space
    fragNormal = normalize(mat3(ubo.normalMatrix) * inNormal);
    
    // Pass through texture coordinates
    fragTexCoord = inTexCoord;
    
    // Calculate TBN matrix for normal mapping
    fragTangent = normalize(mat3(ubo.normalMatrix) * inTangent);
    fragBitangent = cross(fragNormal, fragTangent);
    
    // Transform to clip space
    gl_Position = ubo.proj * ubo.view * worldPos;
}

