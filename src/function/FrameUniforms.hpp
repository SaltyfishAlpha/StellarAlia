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
    float     _pad0;        // 284..288
    // L0+L1+L2 SH coefficients for diffuse irradiance, Lambertian-convolved.
    // std140: vec3 → vec4; .w is unused padding.
    glm::vec4 irrSH[9];     // 288..432
};
static_assert(sizeof(FrameUniforms) == 432,
    "FrameUniforms size mismatch with frame_uniforms.glsl set=0 binding=0");

// ─────────────────────────────────────────────────────────────────────────────
// set=0, binding=1  LightData
// Must match assets/shaders/common/frame_uniforms.glsl exactly.
//
// Supports up to MAX_LIGHTS lights of three types:
//   type=0  Directional  — direction, color, intensity
//   type=1  Point        — position, color, intensity, range
//   type=2  Spot         — position, direction, color, intensity, range, angles
// ─────────────────────────────────────────────────────────────────────────────
struct LightEntry {
    glm::vec3 direction;   float intensity;   //  0..16  (dir/spot: light direction; point: unused)
    glm::vec3 color;       float range;       // 16..32  (point/spot: falloff range; dir: 0)
    glm::vec3 position;    int   type;        // 32..48  (point/spot: world position; dir: unused)
    float     innerAngle;  float outerAngle;  // 48..56  (spot only; radians from axis)
    float     _pad0;       float _pad1;       // 56..64
};
static_assert(sizeof(LightEntry) == 64, "LightEntry must be 64 bytes (std140 array element)");

struct alignas(16) LightUniforms {
    static constexpr int MAX_LIGHTS = 8;

    int   lightCount;                    //  0..4
    float _pad0, _pad1, _pad2;           //  4..16
    LightEntry lights[MAX_LIGHTS];       // 16..528
};
static_assert(sizeof(LightUniforms) == 16 + 8 * 64,
    "LightUniforms size mismatch with frame_uniforms.glsl set=0 binding=1");

} // namespace StellarAlia
