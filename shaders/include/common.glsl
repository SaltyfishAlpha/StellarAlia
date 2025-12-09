// common.glsl
// Common shader definitions and utilities

#ifndef COMMON_GLSL
#define COMMON_GLSL

// Common constants
#define PI 3.14159265359
#define EPSILON 0.0001

// Common data structures
struct Material {
    vec3 albedo;
    float metallic;
    float roughness;
    vec3 emissive;
};

struct Light {
    vec3 position;
    vec3 color;
    float intensity;
    float radius;
};

// Utility functions
vec3 calculateNormal(vec3 position, vec2 texCoord) {
    // Placeholder for normal calculation
    return vec3(0.0, 1.0, 0.0);
}

float linearizeDepth(float depth, float near, float far) {
    return (2.0 * near) / (far + near - depth * (far - near));
}

// Encode/decode functions for G-Buffer
vec2 encodeNormal(vec3 n) {
    return n.xy * 0.5 + 0.5;
}

vec3 decodeNormal(vec2 enc) {
    vec2 fenc = enc * 4.0 - 2.0;
    float f = dot(fenc, fenc);
    float g = sqrt(1.0 - f / 4.0);
    vec3 n;
    n.xy = fenc * g;
    n.z = 1.0 - f / 2.0;
    return n;
}

#endif // COMMON_GLSL

