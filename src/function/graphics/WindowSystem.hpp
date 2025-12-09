#pragma once

/**
 * @file WindowSystem.hpp
 * @brief GLFW-based window system
 */

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cstdint>
#include <memory>

namespace StellarAlia::Function::Graphics {

    /**
     * @brief Window system creation parameters
     */
    struct WindowSystemCreateInfo {
        uint32_t width = 1280;
        uint32_t height = 720;
        const char* title = "StellarAlia Application";
        bool resizable = true;
        bool fullscreen = false;
    };

    /**
     * @brief Concrete GLFW window system
     */
    class WindowSystem {
    public:
        WindowSystem();
        ~WindowSystem();

        /**
         * @brief Initialize the window system
         * @param createInfo Creation parameters
         * @return True if initialization succeeded, false otherwise
         */
        bool Initialize(const WindowSystemCreateInfo& createInfo);

        /**
         * @brief Shutdown and cleanup the window system
         */
        void Shutdown();

        /**
         * @brief Poll window events
         * @return True if window should continue, false if should close
         */
        bool PollEvents();

        /**
         * @brief Get the native window handle (for graphics API surface creation)
         * @return Platform-specific window handle
         */
        GLFWwindow* GetNativeHandle() const;

        /**
         * @brief Get the window width
         * @return Width in pixels
         */
        uint32_t GetWidth() const;

        /**
         * @brief Get the window height
         * @return Height in pixels
         */
        uint32_t GetHeight() const;

        /**
         * @brief Check if the window should close
         * @return True if window should close
         */
        bool ShouldClose() const;

        /**
         * @brief Set the window title
         * @param title New window title
         */
        void SetTitle(const char* title);

        /**
         * @brief Check if the window was resized (since last check)
         * @return True if window was resized
         */
        bool WasResized() const;

    protected:
        GLFWwindow* m_window = nullptr;
        uint32_t m_width = 0;
        uint32_t m_height = 0;
        mutable bool m_wasResized = false;
        mutable bool m_shouldClose = false;

        WindowSystem(const WindowSystem&) = delete;
        WindowSystem& operator=(const WindowSystem&) = delete;

        static void FramebufferResizeCallback(GLFWwindow* window, int width, int height);
    };

} // namespace StellarAlia::Function::Graphics

