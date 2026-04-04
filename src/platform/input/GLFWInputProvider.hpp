#pragma once

#include "platform/input/IInputProvider.hpp"
#include <cstdint>

// Forward-declare GLFWwindow to keep GLFW headers out of every TU.
struct GLFWwindow;

namespace StellarAlia::Platform {

// ─────────────────────────────────────────────────────────────────────────────
// GLFWInputProvider — GLFW-backed IInputProvider
//
// Ownership: non-owning; the GLFWwindow must outlive this object.
//
// Frame contract (same thread):
//   window->PollEvents();       // fires GLFW scroll/key callbacks
//   inputProvider.Poll();       // snapshots keyboard, mouse delta, gamepad
//   // ... read Get* values ...
//
// Supported paths (Phase 1):
//   Keyboard/<KeyName>          e.g. "Keyboard/W", "Keyboard/Space"
//   Mouse/LeftButton            Mouse/RightButton   Mouse/MiddleButton
//   Mouse/Delta                 Mouse/Position      Mouse/ScrollY
//   Gamepad/LeftStick           Gamepad/RightStick  Gamepad/DPad
//   Gamepad/LeftTrigger         Gamepad/RightTrigger
//   Gamepad/ButtonSouth/North/East/West
//   Gamepad/LeftBumper          Gamepad/RightBumper
//   Gamepad/LeftThumb           Gamepad/RightThumb
//   Gamepad/Start               Gamepad/Select
//
// Joystick index 0 (GLFW_JOYSTICK_1) is used for gamepad support.
// Stick Y axes are negated so +Y = up, matching typical game convention.
// ─────────────────────────────────────────────────────────────────────────────
class GLFWInputProvider final : public IInputProvider {
public:
    explicit GLFWInputProvider(GLFWwindow* window);
    ~GLFWInputProvider() override;

    // Non-copyable, non-movable (registered in static scroll callback map).
    GLFWInputProvider(const GLFWInputProvider&)            = delete;
    GLFWInputProvider& operator=(const GLFWInputProvider&) = delete;

    void      Poll() override;
    float     GetButton(std::string_view path) const override;
    float     GetAxis  (std::string_view path) const override;
    glm::vec2 GetAxis2D(std::string_view path) const override;

    // Capture mode — GLFW_CURSOR_DISABLED: cursor is hidden and locked, raw
    // mouse delta is provided by the OS without ballistics or edge clamping.
    // This is the correct mode for FPS-style look (eliminates OS-acceleration
    // jitter). When released, cursor is restored to GLFW_CURSOR_NORMAL and the
    // stored cursor position is re-initialised to avoid a spurious delta spike.
    void SetCursorCapture(bool capture);

    // Cursor wrap: when enabled, Poll() teleports the cursor to the opposite
    // edge when it reaches a window boundary. Ignored in capture mode.
    void SetCursorWrap(bool enable);
    // Hide/show the OS cursor without capturing it (HIDDEN vs NORMAL mode).
    // For FPS look, prefer SetCursorCapture which also removes OS acceleration.
    void SetCursorVisible(bool visible);

private:
    static void ScrollCallback(GLFWwindow* w, double dx, double dy);

    GLFWwindow* m_window = nullptr;

    glm::vec2 m_cursorPos  = {};
    glm::vec2 m_mouseDelta = {};
    float     m_scrollDelta = 0.f;
    float     m_scrollAccum = 0.f;

    bool m_cursorCapture = false;
    bool m_cursorWrap    = false;

    // Mirrors GLFWgamepadstate without requiring the GLFW header here.
    struct GamepadSnapshot {
        uint8_t buttons[15] = {};
        float   axes[6]     = {};
        bool    valid        = false;
    } m_gamepad;
};

} // namespace StellarAlia::Platform
