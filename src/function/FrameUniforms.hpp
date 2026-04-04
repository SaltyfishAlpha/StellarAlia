#pragma once

#include <glm/glm.hpp>

namespace StellarAlia {

// ─────────────────────────────────────────────────────────────────────────────
// set=0, binding=0  FrameData
// Must match assets/shaders/common/frame_uniforms.glsl exactly.
// ─────────────────────────────────────────────────────────────────────────────
struct alignas(16) FrameUniforms {
    glm::mat4 view;         //   0..63
    glm::mat4 proj;         //  64..127
    glm::mat4 viewProj;     // 128..191
    glm::mat4 invViewProj;  // 192..255
    glm::vec3 cameraPos;    // 256..268
    float     time;         // 269..272  (completes vec4)
    glm::vec2 resolution;   // 272..280
    float     deltaTime;    // 280..284
    float     shadowBias;   // 284..288  (depth bias for directional shadow)
    // L0+L1+L2 SH coefficients for diffuse irradiance, Lambertian-convolved.
    // std140: vec3 → vec4; .w is unused padding.
    glm::vec4 irrSH[9];         // 288..432
    // Orthographic light view-projection for the directional shadow map.
    glm::mat4 lightSpaceMatrix; // 432..496
};
static_assert(sizeof(FrameUniforms) == 496,
    "FrameUniforms size mismatch with frame_uniforms.glsl set=0 binding=0");

// ─────────────────────────────────────────────────────────────────────────────
// set=0, binding=1  LightData
// Must match assets/shaders/common/frame_uniforms.glsl exactly.
//
// Supports up to MAX_LIGHTS lights of four types:
//   type=0  Directional  — direction, color, intensity
//   type=1  Point        — position, color, intensity, range
//   type=2  Spot         — position, direction, color, intensity, range, innerAngle, outerAngle
//   type=3  Area (rect)  — position, tangentU, tangentV, color, intensity
//                          innerAngle/outerAngle slots reused as areaWidth/areaHeight
//
// Field reuse map:
//   innerAngle  → spot: inner cone angle (rad)   | area: rectangle width
//   outerAngle  → spot: outer cone angle (rad)   | area: rectangle height
//   tangentU    → area: local X axis (world space, right edge direction)
//   tangentV    → area: local Y axis (world space, up edge direction)
// ─────────────────────────────────────────────────────────────────────────────
struct LightEntry {
    glm::vec3 direction;   float intensity;   //   0..16  dir/spot: direction; area/point: unused
    glm::vec3 color;       float range;       //  16..32  point/spot: falloff range; dir/area: 0
    glm::vec3 position;    int   type;        //  32..48  type: 0=dir 1=point 2=spot 3=area
    float     innerAngle;  float outerAngle;  //  48..56  spot: cone (rad); area: width, height
    float     _align0;     float _align1;     //  56..64  — std140 alignment gap: vec3 requires
                                              //           offset%16==0, previous field ends at 56
    glm::vec3 tangentU;    float _pad0;       //  64..80  area: right axis; others: zero
    glm::vec3 tangentV;    float _pad1;       //  80..96  area: up axis; others: zero
    // std140 array stride = ceil(96/16)*16 = 96 — no trailing padding needed
};
static_assert(sizeof(LightEntry) == 96, "LightEntry must be 96 bytes (std140 array element)");

struct alignas(16) LightUniforms {
    static constexpr int MAX_LIGHTS = 8;

    int   lightCount;                    //  0..4
    float _pad0, _pad1, _pad2;           //  4..16
    LightEntry lights[MAX_LIGHTS];       // 16..784
};
static_assert(sizeof(LightUniforms) == 16 + 8 * 96,   // 784 bytes total
    "LightUniforms size mismatch with frame_uniforms.glsl set=0 binding=1");

} // namespace StellarAlia
