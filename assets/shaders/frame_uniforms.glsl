// ─────────────────────────────────────────────────────────────────────────────
// set=1 — Per-Frame Descriptors (Issue #72 aligned with UE5 / Unity HDRP)
//
// Bound ONCE per pass after the first SetPipeline.
// ALL engine shaders share this exact layout — never change binding indices.
// C++ mirror: src/function/FrameUniforms.hpp
//
// Set assignment convention (Issue #72):
//   set=0  BindlessTextureHeap   (bind once per cmd buffer)
//   set=1  FrameUniforms          (this file)
//   set=2  MaterialParams SSBO_DYN
//   set=3  Skin / per-object
// ─────────────────────────────────────────────────────────────────────────────
#ifndef SA_FRAME_UNIFORMS_GLSL
#define SA_FRAME_UNIFORMS_GLSL

// binding=0  Camera / time / screen
layout(set = 1, binding = 0) uniform FrameData {
    mat4  view;
    mat4  proj;
    mat4  viewProj;
    mat4  invViewProj;
    mat4  invProj;
    vec3  cameraPos;   float time;
    vec2  resolution;  float deltaTime;  float shadowBias;
    // L0+L1+L2 spherical harmonic coefficients for diffuse irradiance.
    // Pre-multiplied by the Lambertian convolution kernel (Ramamoorthi & Hanrahan 2001).
    // Evaluation: irradiance(N) ≈ sum_i(irrSH[i].rgb * Y_i(N))
    // w component unused (std140 vec3 → vec4 padding).
    vec4  irrSH[9];
    mat4  lightSpaceMatrix;       // orthographic light view-projection for shadow pass
    // TAA / motion blur temporal data
    mat4  prevViewProj;           // previous frame unjittered viewProj
    mat4  currUnjitteredViewProj; // Issue #85: unjittered current viewProj for VelocityPrepass
    vec2  jitter;                 // current Halton jitter in pixel space [-0.5, 0.5]
    uint  frameIndex;             // frame counter mod 256
    float volFogFar;              // Issue #49: froxel far end (m); 1.0 when fog off
} u_Frame;

// binding=1  Light list, up to MAX_LIGHTS entries.
// type: 0=Directional, 1=Point, 2=Spot, 3=Area (rect, LTC).
// Field reuse: innerAngle/outerAngle → area: width, height
//              tangentU/tangentV     → area: rectangle right/up axes (world space)
// Must match src/function/FrameUniforms.hpp LightUniforms exactly (96 bytes/entry).
// std140 places vec3 at 16-byte-aligned offsets; after outerAngle (ends at 56),
// the runtime inserts an implicit 8-byte gap before tangentU at offset 64.
#define MAX_LIGHTS 8
struct LightEntry {
    vec3  direction;   float intensity;   //  0..16  dir/spot: direction; area/point: unused
    vec3  color;       float range;       // 16..32  point/spot: falloff range; dir/area: 0
    vec3  position;    int   type;        // 32..48  type: 0=dir 1=point 2=spot 3=area
    float innerAngle;  float outerAngle;  // 48..56  spot: cone (rad); area: width, height
    // [56..63] implicit std140 alignment gap (vec3 needs offset%16==0)
    vec3  tangentU;    float twoSided;    // 64..80  area: right axis + two-sided flag (0/1); others: zero
    vec3  tangentV;    float _pad1;       // 80..96  area: up axis; others: zero
    // stride = ceil(96/16)*16 = 96 — no trailing padding
};
layout(set = 1, binding = 1) uniform LightData {
    int        lightCount;
    float      _pad0; float _pad1; float _pad2;
    LightEntry lights[MAX_LIGHTS];
} u_Lights;

// binding=2  BRDF LUT (NdotV × roughness → (scale, bias) for Schlick split-sum)
layout(set = 1, binding = 2) uniform sampler2D t_BrdfLut;

// binding=3  Prefiltered specular env — cubemap RGBA32F, mip chain.
//            mip 0 = roughness 0.0 (mirror), mip 4 = roughness 1.0 (fully diffuse).
layout(set = 1, binding = 3) uniform samplerCube t_PrefilteredEnv;

// binding=4  Skybox cubemap (full resolution, no prefiltering) — used by skybox pass.
layout(set = 1, binding = 4) uniform samplerCube t_SkyboxMap;

// binding=5  LTC area-light matrix LUT  (64×64 RGBA32F)
//            uv = (NdotV, roughness) → inverse M matrix (packed as mat3 in 4 texels via atlas, or vec4 row-major)
layout(set = 1, binding = 5) uniform sampler2D t_LtcMat;

// binding=6  LTC area-light amplitude LUT (64×64 RGBA32F)
//            uv = (NdotV, roughness) → (GGX norm, Fresnel, sphere horizon clip, unused)
layout(set = 1, binding = 6) uniform sampler2D t_LtcAmp;

// binding=7  Directional shadow map (D32, appended Issue #56).
//            Forward passes sample it from here; deferred lighting keeps its
//            own copy at set=2 binding=4. Appended — never renumber above.
layout(set = 1, binding = 7) uniform sampler2D t_ShadowMap;

// binding=8  Volumetric fog integrated volume (appended Issue #49 Step 9).
//            rgb = in-scatter to this depth, a = transmittance. Forward
//            transparents sample per fragment; a 1×1×2 (0,0,0,1) dummy is
//            bound when fog is off, making the composite a no-op.
layout(set = 1, binding = 8) uniform sampler3D t_FogVolume;

#endif // SA_FRAME_UNIFORMS_GLSL
