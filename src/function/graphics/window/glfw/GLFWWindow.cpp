#include "function/graphics/window/glfw/GLFWWindow.hpp"
#include "core/logs/Log.hpp"
#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace StellarAlia::Function::Graphics::Window {

    GLFWWindow::GLFWWindow() = default;

    GLFWWindow::~GLFWWindow() {
        Shutdown();
    }

    bool GLFWWindow::Initialize(const WindowCreateInfo& createInfo) {
        if (m_window) {
            SA_LOG_WARN("GLFWWindow already initialized");
            return false;
        }

        // Initialize GLFW if not already initialized
        if (!glfwInit()) {
            SA_LOG_ERROR("Failed to initialize GLFW");
            return false;
        }

        // GLFW doesn't create OpenGL context by default for Vulkan
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        
        if (!createInfo.resizable) {
            glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        }

        m_window = glfwCreateWindow(
            static_cast<int>(createInfo.width),
            static_cast<int>(createInfo.height),
            createInfo.title,
            createInfo.fullscreen ? glfwGetPrimaryMonitor() : nullptr,
            nullptr
        );

        if (!m_window) {
            SA_LOG_ERROR("Failed to create GLFW window");
            glfwTerminate();
            return false;
        }

        // Set user pointer for callbacks
        glfwSetWindowUserPointer(m_window, this);
        
        // Set framebuffer resize callback
        glfwSetFramebufferSizeCallback(m_window, FramebufferResizeCallback);

        m_width = createInfo.width;
        m_height = createInfo.height;

        SA_LOG_INFO("GLFW window created: {}x{}", m_width, m_height);
        return true;
    }

    void GLFWWindow::Shutdown() {
        if (m_window) {
            glfwDestroyWindow(m_window);
            m_window = nullptr;
        }
        glfwTerminate();
    }

    bool GLFWWindow::PollEvents() {
        glfwPollEvents();
        return !glfwWindowShouldClose(m_window);
    }

    void* GLFWWindow::GetNativeHandle() const {
        return m_window;
    }

    const char* const* GLFWWindow::GetVulkanInstanceExtensions(uint32_t* extensionCount) const {
        if (!m_window) {
            *extensionCount = 0;
            return nullptr;
        }

        uint32_t count = 0;
        const char** extensions = glfwGetRequiredInstanceExtensions(&count);

        if (!extensions || count == 0) {
            SA_LOG_ERROR("Failed to get Vulkan instance extensions from GLFW");
            *extensionCount = 0;
            return nullptr;
        }

        m_cachedExtensions.assign(extensions, extensions + count);
        *extensionCount = count;
        return m_cachedExtensions.data();
    }

    bool GLFWWindow::CreateVulkanSurface(VkInstance instance, const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface) const {
        if (!m_window) {
            return false;
        }

        VkResult result = glfwCreateWindowSurface(instance, m_window, allocator, surface);
        if (result != VK_SUCCESS) {
            SA_LOG_ERROR("Failed to create Vulkan surface from GLFW window: VkResult = {}", static_cast<int>(result));
            return false;
        }

        return true;
    }

    uint32_t GLFWWindow::GetWidth() const {
        if (m_window) {
            int w, h;
            glfwGetFramebufferSize(m_window, &w, &h);
            return static_cast<uint32_t>(w);
        }
        return m_width;
    }

    uint32_t GLFWWindow::GetHeight() const {
        if (m_window) {
            int w, h;
            glfwGetFramebufferSize(m_window, &w, &h);
            return static_cast<uint32_t>(h);
        }
        return m_height;
    }

    bool GLFWWindow::ShouldClose() const {
        return m_window ? glfwWindowShouldClose(m_window) : true;
    }

    void GLFWWindow::SetTitle(const char* title) {
        if (m_window) {
            glfwSetWindowTitle(m_window, title);
        }
    }

    void GLFWWindow::FramebufferResizeCallback(GLFWwindow* window, int width, int height) {
        auto* glfwWindow = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
        if (glfwWindow) {
            glfwWindow->OnFramebufferResize(width, height);
        }
    }

    void GLFWWindow::OnFramebufferResize(int width, int height) {
        m_width = static_cast<uint32_t>(width);
        m_height = static_cast<uint32_t>(height);
        m_wasResized = true;
    }

    bool GLFWWindow::WasResized() const {
        bool resized = m_wasResized;
        m_wasResized = false;  // Reset after checking
        return resized;
    }

} // namespace StellarAlia::Function::Graphics::Window

