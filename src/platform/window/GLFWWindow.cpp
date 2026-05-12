#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "platform/window/GLFWWindow.hpp"
#include "core/logs/Log.hpp"

namespace StellarAlia::Platform {

std::unique_ptr<GLFWWindow> GLFWWindow::Create(const WindowDesc& desc) {
    if (!glfwInit()) {
        SA_LOG_CRITICAL("GLFWWindow: glfwInit() failed");
        return nullptr;
    }

    // No OpenGL context — we manage rendering through Vulkan
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, desc.resizable ? GLFW_TRUE : GLFW_FALSE);

    GLFWwindow* handle = glfwCreateWindow(
        static_cast<int>(desc.width),
        static_cast<int>(desc.height),
        desc.title,
        nullptr, nullptr);

    if (!handle) {
        SA_LOG_CRITICAL("GLFWWindow: glfwCreateWindow() failed");
        glfwTerminate();
        return nullptr;
    }

    auto win     = std::unique_ptr<GLFWWindow>(new GLFWWindow());
    win->m_handle = handle;
    win->m_width  = desc.width;
    win->m_height = desc.height;

    // Store pointer-to-self so callbacks can update state
    glfwSetWindowUserPointer(handle, win.get());
    glfwSetFramebufferSizeCallback(handle, OnFramebufferResized);
    glfwSetWindowFocusCallback(handle, OnWindowFocus);

    SA_LOG_INFO("GLFWWindow: created {}x{} '{}'", desc.width, desc.height, desc.title);
    return win;
}

GLFWWindow::~GLFWWindow() {
    if (m_handle) {
        glfwDestroyWindow(m_handle);
        glfwTerminate();
        SA_LOG_INFO("GLFWWindow: destroyed");
    }
}

bool GLFWWindow::ShouldClose() const {
    return glfwWindowShouldClose(m_handle) != 0;
}

void GLFWWindow::PollEvents() {
    glfwPollEvents();
}

void* GLFWWindow::GetNativeHandle() const {
    return m_handle;
}

void GLFWWindow::OnFramebufferResized(GLFWwindow* window, int width, int height) {
    auto* self    = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
    self->m_width  = static_cast<uint32_t>(width);
    self->m_height = static_cast<uint32_t>(height);
    SA_LOG_INFO("GLFWWindow: resized to {}x{}", width, height);
}

void GLFWWindow::OnWindowFocus(GLFWwindow* window, int focused) {
    auto* self   = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
    self->m_focused = (focused != 0);
}

} // namespace StellarAlia::Platform
