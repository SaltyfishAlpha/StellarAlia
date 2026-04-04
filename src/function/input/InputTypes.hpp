#pragma once

#include <glm/glm.hpp>

namespace StellarAlia {

// ── Value type of an Action ───────────────────────────────────────────────────

enum class ActionType { Button, Axis, Axis2D };

// ── Device family for last-active-device priority ────────────────────────────

enum class DeviceFamily { KeyboardMouse, Gamepad, Unknown };

// ── Per-frame snapshot of one Action's resolved value ────────────────────────
//
// activatedThisFrame   = was inactive last frame, active this frame
// deactivatedThisFrame = was active last frame, inactive this frame
// "active" for Button/Axis: |valueFloat| > activationThreshold
// "active" for Axis2D:      length(valueVec2) > activationThreshold

struct ActionState {
    float     valueFloat = 0.f;
    glm::vec2 valueVec2  = {};
    bool active              = false;
    bool activatedThisFrame   = false;
    bool deactivatedThisFrame = false;
};

// ── Value processor chain (no heap allocation per processor) ─────────────────
//
// Processors are applied in order to the raw binding value before it reaches
// the Action. Each Step is a plain-data descriptor; Apply() evaluates the
// chain with no virtual dispatch.

struct ProcessorChain {
    struct Step {
        enum class Type { Scale, DeadZone, Invert, Clamp, Normalize };
        Type  type = Type::Scale;
        float x    = 1.f;  // Scale: x-factor; DeadZone: min; Clamp: lo
        float y    = 1.f;  // Scale: y-factor; Clamp: hi
    };

    // Fluent builder methods (return *this for chaining in BindingDef factories).
    ProcessorChain& Scale(float uniform);
    ProcessorChain& Scale(float x, float y);
    ProcessorChain& DeadZone(float min);
    ProcessorChain& Invert();
    ProcessorChain& Clamp(float lo, float hi);
    ProcessorChain& Normalize();

    // Apply to scalar or 2D value; returns processed result.
    float     Apply(float v)     const;
    glm::vec2 Apply(glm::vec2 v) const;

    std::vector<Step> steps;
};

} // namespace StellarAlia

// ── Inline ProcessorChain implementation ──────────────────────────────────────

#include <vector>
#include <cmath>
#include <algorithm>

namespace StellarAlia {

inline ProcessorChain& ProcessorChain::Scale(float u)         { steps.push_back({Step::Type::Scale,    u, u});  return *this; }
inline ProcessorChain& ProcessorChain::Scale(float x, float y){ steps.push_back({Step::Type::Scale,    x, y});  return *this; }
inline ProcessorChain& ProcessorChain::DeadZone(float mn)     { steps.push_back({Step::Type::DeadZone, mn, 0}); return *this; }
inline ProcessorChain& ProcessorChain::Invert()               { steps.push_back({Step::Type::Invert,   0, 0});  return *this; }
inline ProcessorChain& ProcessorChain::Clamp(float lo, float hi){ steps.push_back({Step::Type::Clamp,  lo, hi}); return *this; }
inline ProcessorChain& ProcessorChain::Normalize()            { steps.push_back({Step::Type::Normalize,0, 0});  return *this; }

inline float ProcessorChain::Apply(float v) const {
    for (const auto& s : steps) {
        switch (s.type) {
        case Step::Type::Scale:
            v *= s.x;
            break;
        case Step::Type::DeadZone: {
            const float a = std::abs(v);
            v = (a < s.x) ? 0.f : std::copysign((a - s.x) / (1.f - s.x), v);
            break;
        }
        case Step::Type::Invert:
            v = -v;
            break;
        case Step::Type::Clamp:
            v = std::clamp(v, s.x, s.y);
            break;
        case Step::Type::Normalize:
            v = std::clamp(v, -1.f, 1.f);
            break;
        }
    }
    return v;
}

inline glm::vec2 ProcessorChain::Apply(glm::vec2 v) const {
    for (const auto& s : steps) {
        switch (s.type) {
        case Step::Type::Scale:
            v.x *= s.x;
            v.y *= s.y;
            break;
        case Step::Type::DeadZone: {
            const float len = glm::length(v);
            v = (len < s.x) ? glm::vec2{} : v * ((len - s.x) / ((1.f - s.x) * len));
            break;
        }
        case Step::Type::Invert:
            v = -v;
            break;
        case Step::Type::Clamp:
            v.x = std::clamp(v.x, s.x, s.y);
            v.y = std::clamp(v.y, s.x, s.y);
            break;
        case Step::Type::Normalize: {
            const float len = glm::length(v);
            if (len > 1.f) v /= len;
            break;
        }
        }
    }
    return v;
}

} // namespace StellarAlia
