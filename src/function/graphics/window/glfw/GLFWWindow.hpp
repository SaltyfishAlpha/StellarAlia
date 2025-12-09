#pragma once

/**
 * @file GLFWWindow.hpp
 * @brief GLFW implementation of Window
 */

#include "function/graphics/window/Window.hpp"
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vector>

namespace StellarAlia::Function::Graphics::Window {

    /**
     * @brief GLFW window implementation
     */
    class GLFWWindow : public Window {
    public:
        GLFWWindow();
        ~GLFWWindow() override;

        bool Initialize(const WindowCreateInfo& createInfo) override;
        void Shutdown() override;
        bool PollEvents() override;
        void* GetNativeHandle() const override;
        const char* const* GetVulkanInstanceExtensions(uint32_t* extensionCount) const override;
        bool CreateVulkanSurface(VkInstance instance, const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface) const override;
        WindowBackend GetBackend() const override { return WindowBackend::GLFW; }
        uint32_t GetWidth() const override;
        uint32_t GetHeight() const override;
        bool ShouldClose() const override;
        void SetTitle(const char* title) override;
        bool WasResized() const override;

    private:
        GLFWwindow* m_window = nullptr;
        uint32_t m_width = 0;
        uint32_t m_height = 0;
        mutable std::vector<const char*> m_cachedExtensions;
        mutable bool m_wasResized = false;

        // GLFW callbacks
        static void FramebufferResizeCallback(GLFWwindow* window, int width, int height);
        void OnFramebufferResize(int width, int height);
    };

} // namespace StellarAlia::Function::Graphics::Window

