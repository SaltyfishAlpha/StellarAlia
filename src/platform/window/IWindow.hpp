#pragma once

#include <cstdint>

namespace StellarAlia::Platform {

// ─────────────────────────────────────────────────────────────────────────────
// IWindow
//
// Platform-agnostic window interface.
// The concrete implementation (GLFWWindow...) lives here;
// upper layers only hold an IWindow* and never see the backend type.
// ─────────────────────────────────────────────────────────────────────────────
class IWindow {
public:
    virtual ~IWindow() = default;

    virtual uint32_t GetWidth()  const = 0;
    virtual uint32_t GetHeight() const = 0;
    virtual bool     ShouldClose() const = 0;

    virtual bool IsFocused() const = 0;

    // Process OS events (call once per frame before BeginFrame)
    virtual void PollEvents() = 0;

    // Returns the raw backend handle:
    //   GLFWWindow  → GLFWwindow*
    //   Win32       → HWND
    // Cast in the RHI backend; never cast in upper layers.
    virtual void* GetNativeHandle() const = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// WindowDesc — construction parameters for any IWindow implementation
// ─────────────────────────────────────────────────────────────────────────────
struct WindowDesc {
    uint32_t    width     = 1280;
    uint32_t    height    = 720;
    const char* title     = "StellarAlia";
    bool        resizable = true;
};

} // namespace StellarAlia::Platform
