// ─────────────────────────────────────────────────────────────────────────────
// set=0 — Per-Frame Descriptors
//
// Bound ONCE per frame by the render loop before any draw calls.
// ALL engine shaders share this exact layout — never change binding indices.
// C++ mirror: src/function/FrameUniforms.hpp
// ─────────────────────────────────────────────────────────────────────────────
#ifndef SA_FRAME_UNIFORMS_GLSL
#define SA_FRAME_UNIFORMS_GLSL

// binding=0  Camera / time / screen
layout(set = 0, binding = 0) uniform FrameData {
    mat4  view;
    mat4  proj;
    mat4  viewProj;
    mat4  invViewProj;
    vec3  cameraPos;   float time;
    vec2  resolution;  float deltaTime;  float _pad0;
    // L0+L1+L2 spherical harmonic coefficients for diffuse irradiance.
    // Pre-multiplied by the Lambertian convolution kernel (Ramamoorthi & Hanrahan 2001).
    // Evaluation: irradiance(N) ≈ sum_i(irrSH[i].rgb * Y_i(N))
    // w component unused (std140 vec3 → vec4 padding).
    vec4  irrSH[9];
} u_Frame;

// binding=1  Light list (directional / point / spot), up to MAX_LIGHTS entries.
// type: 0=Directional, 1=Point, 2=Spot.
// Must match src/function/FrameUniforms.hpp LightUniforms exactly.
#define MAX_LIGHTS 8
struct LightEntry {
    vec3  direction;   float intensity;  //  0..16
    vec3  color;       float range;      // 16..32
    vec3  position;    int   type;       // 32..48
    float innerAngle;  float outerAngle; // 48..56
    float _pad0;       float _pad1;      // 56..64
};
layout(set = 0, binding = 1) uniform LightData {
    int        lightCount;
    float      _pad0; float _pad1; float _pad2;
    LightEntry lights[MAX_LIGHTS];
} u_Lights;

// binding=2  BRDF LUT (NdotV × roughness → (scale, bias) for Schlick split-sum)
layout(set = 0, binding = 2) uniform sampler2D t_BrdfLut;

// binding=3  Prefiltered specular env — equirectangular RGBA32F, mip chain.
//            mip 0 = roughness 0.0 (mirror), mip 4 = roughness 1.0 (fully diffuse).
layout(set = 0, binding = 3) uniform sampler2D t_PrefilteredEnv;

// binding=4  Original HDR panorama (full resolution, no prefiltering) — used by skybox.
layout(set = 0, binding = 4) uniform sampler2D t_SkyboxMap;

#endif // SA_FRAME_UNIFORMS_GLSL
