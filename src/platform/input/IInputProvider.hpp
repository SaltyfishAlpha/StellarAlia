#pragma once

#include <string_view>
#include <glm/glm.hpp>

namespace StellarAlia::Platform {

// ─────────────────────────────────────────────────────────────────────────────
// IInputProvider — raw device snapshot interface
//
// Platform layer abstraction for physical input devices. All values are
// normalised to stable ranges regardless of device type:
//   GetButton  → [0, 1]    keyboard keys, mouse buttons, gamepad buttons
//   GetAxis    → [-1, 1]   scroll wheel, triggers (remapped from [-1,1])
//   GetAxis2D  → vec2      mouse delta / position, sticks, d-pad
//
// Device paths use the form "<Device>/<Control>", e.g.:
//   "Keyboard/W"          "Mouse/Delta"     "Gamepad/LeftStick"
//   "Mouse/LeftButton"    "Mouse/ScrollY"   "Gamepad/ButtonSouth"
//
// Poll() must be called once per frame (after glfwPollEvents) before any
// Get* calls. Values remain stable for the rest of that frame.
// ─────────────────────────────────────────────────────────────────────────────
class IInputProvider {
public:
    virtual ~IInputProvider() = default;

    // Snapshot current physical device state (call once per frame).
    virtual void Poll() = 0;

    // Returns [0,1]; 0 for unknown paths or unpressed controls.
    virtual float GetButton(std::string_view path) const = 0;

    // Returns [-1,1]; 0 for unknown paths.
    virtual float GetAxis(std::string_view path) const = 0;

    // Returns vec2; {0,0} for unknown paths.
    virtual glm::vec2 GetAxis2D(std::string_view path) const = 0;
};

} // namespace StellarAlia::Platform
