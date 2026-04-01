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
};
static_assert(sizeof(FrameUniforms) == 288,
    "FrameUniforms size mismatch with frame_uniforms.glsl set=0 binding=0");

// ─────────────────────────────────────────────────────────────────────────────
// set=0, binding=1  LightData
// Must match assets/shaders/common/frame_uniforms.glsl exactly.
// ─────────────────────────────────────────────────────────────────────────────
struct alignas(16) LightUniforms {
    glm::vec3 direction;        //  0..12
    float     intensity;        // 12..16
    glm::vec3 color;            // 16..28
    float     _pad0;            // 28..32
    glm::vec3 ambientColor;     // 32..44
    float     ambientIntensity; // 44..48
};
static_assert(sizeof(LightUniforms) == 48,
    "LightUniforms size mismatch with frame_uniforms.glsl set=0 binding=1");

} // namespace StellarAlia
