#include "platform/input/GLFWInputProvider.hpp"

#include <GLFW/glfw3.h>
#include <unordered_map>
#include <string_view>
#include <cstring>
#include <cassert>

namespace StellarAlia::Platform {

// ── Static scroll-callback registry ──────────────────────────────────────────

static std::unordered_map<GLFWwindow*, GLFWInputProvider*> s_providers;

void GLFWInputProvider::ScrollCallback(GLFWwindow* w, double /*dx*/, double dy) {
    auto it = s_providers.find(w);
    if (it != s_providers.end())
        it->second->m_scrollAccum += static_cast<float>(dy);
}

// ── Key name → GLFW key code ──────────────────────────────────────────────────

static int ResolveKey(std::string_view name) {
    // clang-format off
    static const std::unordered_map<std::string_view, int> kMap = {
        // Printable
        {"Space",        GLFW_KEY_SPACE},
        {"Apostrophe",   GLFW_KEY_APOSTROPHE},
        {"Comma",        GLFW_KEY_COMMA},
        {"Minus",        GLFW_KEY_MINUS},
        {"Period",       GLFW_KEY_PERIOD},
        {"Slash",        GLFW_KEY_SLASH},
        {"Semicolon",    GLFW_KEY_SEMICOLON},
        {"Equal",        GLFW_KEY_EQUAL},
        {"LeftBracket",  GLFW_KEY_LEFT_BRACKET},
        {"Backslash",    GLFW_KEY_BACKSLASH},
        {"RightBracket", GLFW_KEY_RIGHT_BRACKET},
        {"GraveAccent",  GLFW_KEY_GRAVE_ACCENT},
        // Digits
        {"0",GLFW_KEY_0},{"1",GLFW_KEY_1},{"2",GLFW_KEY_2},{"3",GLFW_KEY_3},{"4",GLFW_KEY_4},
        {"5",GLFW_KEY_5},{"6",GLFW_KEY_6},{"7",GLFW_KEY_7},{"8",GLFW_KEY_8},{"9",GLFW_KEY_9},
        // Letters
        {"A",GLFW_KEY_A},{"B",GLFW_KEY_B},{"C",GLFW_KEY_C},{"D",GLFW_KEY_D},{"E",GLFW_KEY_E},
        {"F",GLFW_KEY_F},{"G",GLFW_KEY_G},{"H",GLFW_KEY_H},{"I",GLFW_KEY_I},{"J",GLFW_KEY_J},
        {"K",GLFW_KEY_K},{"L",GLFW_KEY_L},{"M",GLFW_KEY_M},{"N",GLFW_KEY_N},{"O",GLFW_KEY_O},
        {"P",GLFW_KEY_P},{"Q",GLFW_KEY_Q},{"R",GLFW_KEY_R},{"S",GLFW_KEY_S},{"T",GLFW_KEY_T},
        {"U",GLFW_KEY_U},{"V",GLFW_KEY_V},{"W",GLFW_KEY_W},{"X",GLFW_KEY_X},{"Y",GLFW_KEY_Y},
        {"Z",GLFW_KEY_Z},
        // Control
        {"Escape",       GLFW_KEY_ESCAPE},
        {"Return",       GLFW_KEY_ENTER},
        {"Enter",        GLFW_KEY_ENTER},
        {"Tab",          GLFW_KEY_TAB},
        {"Backspace",    GLFW_KEY_BACKSPACE},
        {"Insert",       GLFW_KEY_INSERT},
        {"Delete",       GLFW_KEY_DELETE},
        {"Right",        GLFW_KEY_RIGHT},
        {"Left",         GLFW_KEY_LEFT},
        {"Down",         GLFW_KEY_DOWN},
        {"Up",           GLFW_KEY_UP},
        {"PageUp",       GLFW_KEY_PAGE_UP},
        {"PageDown",     GLFW_KEY_PAGE_DOWN},
        {"Home",         GLFW_KEY_HOME},
        {"End",          GLFW_KEY_END},
        {"CapsLock",     GLFW_KEY_CAPS_LOCK},
        // Modifiers
        {"LeftShift",    GLFW_KEY_LEFT_SHIFT},
        {"LeftControl",  GLFW_KEY_LEFT_CONTROL},
        {"LeftAlt",      GLFW_KEY_LEFT_ALT},
        {"LeftSuper",    GLFW_KEY_LEFT_SUPER},
        {"RightShift",   GLFW_KEY_RIGHT_SHIFT},
        {"RightControl", GLFW_KEY_RIGHT_CONTROL},
        {"RightAlt",     GLFW_KEY_RIGHT_ALT},
        {"RightSuper",   GLFW_KEY_RIGHT_SUPER},
        // Function
        {"F1", GLFW_KEY_F1},{"F2", GLFW_KEY_F2},{"F3", GLFW_KEY_F3},{"F4",  GLFW_KEY_F4},
        {"F5", GLFW_KEY_F5},{"F6", GLFW_KEY_F6},{"F7", GLFW_KEY_F7},{"F8",  GLFW_KEY_F8},
        {"F9", GLFW_KEY_F9},{"F10",GLFW_KEY_F10},{"F11",GLFW_KEY_F11},{"F12",GLFW_KEY_F12},
    };
    // clang-format on
    auto it = kMap.find(name);
    return (it != kMap.end()) ? it->second : GLFW_KEY_UNKNOWN;
}

// ── Gamepad button name → button index ───────────────────────────────────────

static int ResolveGamepadButton(std::string_view name) {
    static const std::unordered_map<std::string_view, int> kMap = {
        {"ButtonSouth",  GLFW_GAMEPAD_BUTTON_A},
        {"ButtonEast",   GLFW_GAMEPAD_BUTTON_B},
        {"ButtonWest",   GLFW_GAMEPAD_BUTTON_X},
        {"ButtonNorth",  GLFW_GAMEPAD_BUTTON_Y},
        {"LeftBumper",   GLFW_GAMEPAD_BUTTON_LEFT_BUMPER},
        {"RightBumper",  GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER},
        {"Select",       GLFW_GAMEPAD_BUTTON_BACK},
        {"Start",        GLFW_GAMEPAD_BUTTON_START},
        {"LeftThumb",    GLFW_GAMEPAD_BUTTON_LEFT_THUMB},
        {"RightThumb",   GLFW_GAMEPAD_BUTTON_RIGHT_THUMB},
        {"DPadUp",       GLFW_GAMEPAD_BUTTON_DPAD_UP},
        {"DPadRight",    GLFW_GAMEPAD_BUTTON_DPAD_RIGHT},
        {"DPadDown",     GLFW_GAMEPAD_BUTTON_DPAD_DOWN},
        {"DPadLeft",     GLFW_GAMEPAD_BUTTON_DPAD_LEFT},
    };
    auto it = kMap.find(name);
    return (it != kMap.end()) ? it->second : -1;
}

// ── Construction / destruction ────────────────────────────────────────────────

GLFWInputProvider::GLFWInputProvider(GLFWwindow* window)
    : m_window(window)
{
    assert(window && "GLFWInputProvider: null window");

    // Initialise cursor position so first-frame delta is zero.
    double cx, cy;
    glfwGetCursorPos(m_window, &cx, &cy);
    m_cursorPos = { static_cast<float>(cx), static_cast<float>(cy) };

    s_providers[m_window] = this;
    glfwSetScrollCallback(m_window, ScrollCallback);
}

GLFWInputProvider::~GLFWInputProvider() {
    glfwSetScrollCallback(m_window, nullptr);
    s_providers.erase(m_window);
}

// ── Poll ──────────────────────────────────────────────────────────────────────

void GLFWInputProvider::Poll() {
    // Mouse delta (cursor position diff since last Poll).
    double cx, cy;
    glfwGetCursorPos(m_window, &cx, &cy);
    glm::vec2 newPos{ static_cast<float>(cx), static_cast<float>(cy) };
    m_mouseDelta = newPos - m_cursorPos;

    // Cursor wrapping — Blender style. Skip in capture mode: GLFW_CURSOR_DISABLED
    // provides unbounded virtual coordinates, so window-edge checks are meaningless
    // and glfwSetCursorPos is a no-op anyway.
    if (m_cursorWrap && !m_cursorCapture) {
        int w, h;
        glfwGetWindowSize(m_window, &w, &h);
        constexpr float kMargin = 2.f;
        bool teleported = false;
        if (newPos.x < kMargin)           { newPos.x = static_cast<float>(w) - kMargin - 1.f; teleported = true; }
        else if (newPos.x > w - kMargin)  { newPos.x = kMargin + 1.f;                          teleported = true; }
        if (newPos.y < kMargin)           { newPos.y = static_cast<float>(h) - kMargin - 1.f; teleported = true; }
        else if (newPos.y > h - kMargin)  { newPos.y = kMargin + 1.f;                          teleported = true; }
        if (teleported)
            glfwSetCursorPos(m_window, static_cast<double>(newPos.x), static_cast<double>(newPos.y));
    }

    m_cursorPos = newPos;

    // Consume accumulated scroll (set by callback during glfwPollEvents).
    m_scrollDelta = m_scrollAccum;
    m_scrollAccum = 0.f;

    // Gamepad snapshot (joystick 0 = GLFW_JOYSTICK_1).
    GLFWgamepadstate gs{};
    m_gamepad.valid = (glfwGetGamepadState(GLFW_JOYSTICK_1, &gs) == GLFW_TRUE);
    if (m_gamepad.valid) {
        static_assert(sizeof(m_gamepad.buttons) == sizeof(gs.buttons));
        static_assert(sizeof(m_gamepad.axes)    == sizeof(gs.axes));
        std::memcpy(m_gamepad.buttons, gs.buttons, sizeof(gs.buttons));
        std::memcpy(m_gamepad.axes,    gs.axes,    sizeof(gs.axes));
    } else {
        std::memset(m_gamepad.buttons, 0, sizeof(m_gamepad.buttons));
        std::memset(m_gamepad.axes,    0, sizeof(m_gamepad.axes));
    }
}

// ── GetButton ─────────────────────────────────────────────────────────────────

float GLFWInputProvider::GetButton(std::string_view path) const {
    if (path.starts_with("Keyboard/")) {
        const int key = ResolveKey(path.substr(9));
        if (key == GLFW_KEY_UNKNOWN) return 0.f;
        return glfwGetKey(m_window, key) == GLFW_PRESS ? 1.f : 0.f;
    }
    if (path == "Mouse/LeftButton")
        return glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT)   == GLFW_PRESS ? 1.f : 0.f;
    if (path == "Mouse/RightButton")
        return glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_RIGHT)  == GLFW_PRESS ? 1.f : 0.f;
    if (path == "Mouse/MiddleButton")
        return glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS ? 1.f : 0.f;
    if (path.starts_with("Gamepad/") && m_gamepad.valid) {
        const auto ctrl = path.substr(8);
        const int idx = ResolveGamepadButton(ctrl);
        if (idx >= 0)
            return m_gamepad.buttons[idx] == GLFW_PRESS ? 1.f : 0.f;
        // Triggers as buttons: remap [-1,1] → [0,1].
        if (ctrl == "LeftTrigger")
            return (m_gamepad.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER]  + 1.f) * 0.5f;
        if (ctrl == "RightTrigger")
            return (m_gamepad.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] + 1.f) * 0.5f;
    }
    return 0.f;
}

// ── GetAxis ───────────────────────────────────────────────────────────────────

float GLFWInputProvider::GetAxis(std::string_view path) const {
    if (path == "Mouse/ScrollY")
        return m_scrollDelta;
    if (path.starts_with("Gamepad/") && m_gamepad.valid) {
        const auto ctrl = path.substr(8);
        if (ctrl == "LeftTrigger")
            return (m_gamepad.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER]  + 1.f) * 0.5f;
        if (ctrl == "RightTrigger")
            return (m_gamepad.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] + 1.f) * 0.5f;
    }
    return 0.f;
}

// ── GetAxis2D ─────────────────────────────────────────────────────────────────

glm::vec2 GLFWInputProvider::GetAxis2D(std::string_view path) const {
    if (path == "Mouse/Delta")    return m_mouseDelta;
    if (path == "Mouse/Position") return m_cursorPos;
    if (path.starts_with("Gamepad/") && m_gamepad.valid) {
        const auto ctrl = path.substr(8);
        // Negate Y so +Y = up (GLFW Y is positive-down).
        if (ctrl == "LeftStick")
            return { m_gamepad.axes[GLFW_GAMEPAD_AXIS_LEFT_X],
                    -m_gamepad.axes[GLFW_GAMEPAD_AXIS_LEFT_Y] };
        if (ctrl == "RightStick")
            return { m_gamepad.axes[GLFW_GAMEPAD_AXIS_RIGHT_X],
                    -m_gamepad.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y] };
        if (ctrl == "DPad") {
            const float x =
                (m_gamepad.buttons[GLFW_GAMEPAD_BUTTON_DPAD_RIGHT] == GLFW_PRESS ? 1.f : 0.f) -
                (m_gamepad.buttons[GLFW_GAMEPAD_BUTTON_DPAD_LEFT]  == GLFW_PRESS ? 1.f : 0.f);
            const float y =
                (m_gamepad.buttons[GLFW_GAMEPAD_BUTTON_DPAD_UP]   == GLFW_PRESS ? 1.f : 0.f) -
                (m_gamepad.buttons[GLFW_GAMEPAD_BUTTON_DPAD_DOWN]  == GLFW_PRESS ? 1.f : 0.f);
            return { x, y };
        }
    }
    return {};
}

// ── Cursor capture / wrap / visibility ───────────────────────────────────────

void GLFWInputProvider::SetCursorCapture(bool capture) {
    if (m_cursorCapture == capture) return;
    m_cursorCapture = capture;
    glfwSetInputMode(m_window, GLFW_CURSOR,
        capture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);

    // Re-initialise stored cursor position so the first delta after a mode
    // change is zero (GLFW's virtual position resets on disable→normal).
    double cx, cy;
    glfwGetCursorPos(m_window, &cx, &cy);
    m_cursorPos = { static_cast<float>(cx), static_cast<float>(cy) };
    m_mouseDelta = {};
}

void GLFWInputProvider::SetCursorWrap(bool enable) {
    m_cursorWrap = enable;
}

void GLFWInputProvider::SetCursorVisible(bool visible) {
    glfwSetInputMode(m_window, GLFW_CURSOR,
        visible ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_HIDDEN);
}

} // namespace StellarAlia::Platform
