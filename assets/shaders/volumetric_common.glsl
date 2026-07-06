// volumetric_common.glsl — froxel helpers shared by the volumetric fog passes
// (Issue #49). Pure functions only — no uniforms or I/O (scatter pass has no
// frame layout, so u_Frame-dependent helpers live in the individual shaders).
//
// Depth distribution: squared — viewDepth(k) = fogFar * (k/N)^2. Front-loads
// slice resolution near the camera without referencing the camera near plane.
#ifndef SA_VOLUMETRIC_COMMON_GLSL
#define SA_VOLUMETRIC_COMMON_GLSL

#include "common.glsl"

float VolFogSliceToDepth(float slice, float numSlices, float fogFar) {
    float t = slice / numSlices;
    return fogFar * t * t;
}

float VolFogDepthToSlice(float viewDepth, float numSlices, float fogFar) {
    return numSlices * sqrt(clamp(viewDepth / fogFar, 0.0, 1.0));
}

// Henyey-Greenstein phase function. cosTheta = dot(light propagation dir,
// direction toward camera) — +1 means the light keeps travelling toward the
// viewer (forward scattering peak when looking into the light).
float VolFogPhaseHG(float cosTheta, float g) {
    float g2    = g * g;
    float denom = max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4);
    return (1.0 - g2) / (4.0 * PI * denom * sqrt(denom));
}

#endif // SA_VOLUMETRIC_COMMON_GLSL
