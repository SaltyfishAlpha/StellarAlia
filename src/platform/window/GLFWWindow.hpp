#pragma once

#include <memory>
#include "platform/window/IWindow.hpp"

// Forward-declare GLFWwindow to avoid leaking GLFW headers into every translation unit
struct GLFWwindow;

namespace StellarAlia::Platform {

// ─────────────────────────────────────────────────────────────────────────────
// GLFWWindow — IWindow backed by GLFW 3
//
// Usage:
//   auto win = GLFWWindow::Create({.width=1280, .height=720, .title="App"});
//   NativeWindowHandle h{win->GetNativeHandle()};
//   auto device = VulkanDevice::Create({.windowHandle = h, ...});
//   while (!win->ShouldClose()) {
//       win->PollEvents();
//       ...
//   }
// ─────────────────────────────────────────────────────────────────────────────
class GLFWWindow final : public IWindow {
public:
    ~GLFWWindow() override;

    [[nodiscard]] static std::unique_ptr<GLFWWindow> Create(const WindowDesc& desc);

    uint32_t GetWidth()  const override { return m_width; }
    uint32_t GetHeight() const override { return m_height; }
    bool     ShouldClose() const override;
    void     PollEvents() override;
    void*    GetNativeHandle() const override;

private:
    GLFWWindow() = default;

    GLFWwindow* m_handle = nullptr;
    uint32_t    m_width  = 0;
    uint32_t    m_height = 0;

    // GLFW resize callback
    static void OnFramebufferResized(GLFWwindow* window, int width, int height);
};

} // namespace StellarAlia::Platform
