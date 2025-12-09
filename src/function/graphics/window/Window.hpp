#pragma once

/**
 * @file Window.hpp
 * @brief Abstract window/surface interface
 * 
 * This provides an abstraction layer for different windowing libraries (SDL2, GLFW, etc.).
 * Implementations should inherit from this interface.
 */

#include <cstdint>
#include <memory>

// Forward declarations
typedef struct VkInstance_T* VkInstance;
typedef struct VkAllocationCallbacks VkAllocationCallbacks;
typedef struct VkSurfaceKHR_T* VkSurfaceKHR;

namespace StellarAlia::Function::Graphics::Window {

    /**
     * @brief Window backend types
     */
    enum class WindowBackend {
        None = 0,
        SDL2,
        GLFW
    };

    /**
     * @brief Window creation parameters
     */
    struct WindowCreateInfo {
        WindowBackend backend = WindowBackend::SDL2;  // Must be explicitly set by user
        uint32_t width = 1280;
        uint32_t height = 720;
        const char* title = "StellarAlia Application";
        bool resizable = true;
        bool fullscreen = false;
    };

    /**
     * @brief Abstract window interface
     * 
     * This is the base class for all windowing implementations.
     */
    class Window {
    public:
        virtual ~Window() = default;

        /**
         * @brief Initialize the window
         * @param createInfo Creation parameters
         * @return True if initialization succeeded, false otherwise
         */
        virtual bool Initialize(const WindowCreateInfo& createInfo) = 0;

        /**
         * @brief Shutdown and cleanup the window
         */
        virtual void Shutdown() = 0;

        /**
         * @brief Poll window events
         * @return True if window should continue, false if should close
         */
        virtual bool PollEvents() = 0;

        /**
         * @brief Get the native window handle (for graphics API surface creation)
         * @return Platform-specific window handle
         */
        virtual void* GetNativeHandle() const = 0;

        /**
         * @brief Get required Vulkan instance extensions
         * @param extensionCount Output: number of extensions
         * @return Array of extension names (nullptr if not supported)
         */
        virtual const char* const* GetVulkanInstanceExtensions(uint32_t* extensionCount) const = 0;

        /**
         * @brief Create Vulkan surface
         * @param instance Vulkan instance
         * @param allocator Vulkan allocator (can be nullptr)
         * @param surface Output: created surface
         * @return True if successful, false otherwise
         */
        virtual bool CreateVulkanSurface(VkInstance instance, const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface) const = 0;

        /**
         * @brief Get the window backend type
         * @return The window backend being used
         */
        virtual WindowBackend GetBackend() const = 0;

        /**
         * @brief Get the window width
         * @return Width in pixels
         */
        virtual uint32_t GetWidth() const = 0;

        /**
         * @brief Get the window height
         * @return Height in pixels
         */
        virtual uint32_t GetHeight() const = 0;

        /**
         * @brief Check if the window should close
         * @return True if window should close
         */
        virtual bool ShouldClose() const = 0;

        /**
         * @brief Set the window title
         * @param title New window title
         */
        virtual void SetTitle(const char* title) = 0;

        /**
         * @brief Check if the window was resized (since last check)
         * @return True if window was resized
         */
        virtual bool WasResized() const = 0;

    protected:
        Window() = default;
        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;
    };

    /**
     * @brief Create a window based on the specified backend
     * @param createInfo Creation parameters
     * @return Unique pointer to the created window, or nullptr on failure
     */
    std::unique_ptr<Window> CreateWindow(const WindowCreateInfo& createInfo);

} // namespace StellarAlia::Function::Graphics::Window

