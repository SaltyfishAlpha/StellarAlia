#include "function/graphics/WindowSystem.hpp"
#include "core/logs/Log.hpp"
#include <algorithm>
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace StellarAlia::Function::Graphics
{
    void WindowSystem::FramebufferResizeCallback(GLFWwindow* window, int width, int height) {
        auto* ws = static_cast<WindowSystem*>(glfwGetWindowUserPointer(window));
        if (!ws) {
            return;
        }
        // Guard against minimized windows reporting zero size; keep at least 1 pixel.
        ws->m_width = static_cast<uint32_t>(std::max(width, 1));
        ws->m_height = static_cast<uint32_t>(std::max(height, 1));
        ws->m_wasResized = true;
    }

    WindowSystem::WindowSystem() = default;

    WindowSystem::~WindowSystem() {
        Shutdown();
    }

    bool WindowSystem::Initialize(const WindowSystemCreateInfo& createInfo) {
        if (m_window) {
            SA_LOG_WARN("WindowSystem already initialized");
            return false;
        }

        if (!glfwInit()) {
            SA_LOG_ERROR("Failed to initialize GLFW");
            return false;
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, createInfo.resizable ? GLFW_TRUE : GLFW_FALSE);

        GLFWmonitor* monitor = nullptr;
        if (createInfo.fullscreen) {
            monitor = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            m_width = mode ? static_cast<uint32_t>(mode->width) : createInfo.width;
            m_height = mode ? static_cast<uint32_t>(mode->height) : createInfo.height;
        } else {
            m_width = createInfo.width;
            m_height = createInfo.height;
        }

        m_window = glfwCreateWindow(static_cast<int>(m_width), static_cast<int>(m_height),
                                    createInfo.title, monitor, nullptr);
        if (!m_window) {
            SA_LOG_ERROR("Failed to create GLFW window");
            glfwTerminate();
            return false;
        }

        glfwSetWindowUserPointer(m_window, this);
        glfwSetFramebufferSizeCallback(m_window, FramebufferResizeCallback);

        return true;
    }

    void WindowSystem::Shutdown() {
        if (m_window) {
            glfwDestroyWindow(m_window);
            m_window = nullptr;
        }
        glfwTerminate();
        m_wasResized = false;
        m_shouldClose = false;
    }

    bool WindowSystem::PollEvents()
    {
        if (!m_window)
        {
            return false;
        }
        glfwPollEvents();
        m_shouldClose = glfwWindowShouldClose(m_window);
        return !m_shouldClose;
    }

    GLFWwindow* WindowSystem::GetNativeHandle() const {
        return m_window;
    }

    uint32_t WindowSystem::GetWidth() const {
        if (!m_window) {
            return 0;
        }
        int w, h;
        glfwGetWindowSize(m_window, &w, &h);
        return static_cast<uint32_t>(w);
    }

    uint32_t WindowSystem::GetHeight() const {
        if (!m_window) {
            return 0;
        }
        int w, h;
        glfwGetWindowSize(m_window, &w, &h);
        return static_cast<uint32_t>(h);
    }

    bool WindowSystem::ShouldClose() const {
        return m_shouldClose || (m_window && glfwWindowShouldClose(m_window));
    }

    void WindowSystem::SetTitle(const char* title) {
        if (m_window && title) {
            glfwSetWindowTitle(m_window, title);
        }
    }

    bool WindowSystem::WasResized() const {
        bool resized = m_wasResized;
        m_wasResized = false;
        return resized;
    }
} // namespace StellarAlia::Function::Graphics

