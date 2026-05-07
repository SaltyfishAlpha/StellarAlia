#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace StellarAlia::Resource {

// ── Skeleton ──────────────────────────────────────────────────────────────────

struct BoneInfo {
    std::string name;
    int32_t     parentIndex       = -1;     // -1 = skeleton root
    glm::mat4   inverseBindMatrix = glm::mat4(1.f);
};

// ── Animation keyframes ───────────────────────────────────────────────────────

struct AnimChannel {
    int32_t boneIndex = -1;

    enum class Target : uint8_t { Translation, Rotation, Scale } target = Target::Translation;
    enum class Interp  : uint8_t { Step, Linear }               interp = Interp::Linear;
    // CubicSpline is downgraded to Linear for simplicity.

    std::vector<float>     times;   // keyframe timestamps in seconds
    std::vector<glm::vec4> values;  // Translation/Scale → xyz0; Rotation → xyzw (glTF order)
};

struct AnimClip {
    std::string              name;
    float                    duration = 0.f;
    std::vector<AnimChannel> channels;
};

// ── Per-vertex skinning data (static, matches rest-pose vertex order) ─────────

struct SkinVertex {
    glm::uvec4 joints  = {0u, 0u, 0u, 0u};  // indices into skeleton bones[]
    glm::vec4  weights = {1.f, 0.f, 0.f, 0.f};
};
static_assert(sizeof(SkinVertex) == 32);

} // namespace StellarAlia::Resource
