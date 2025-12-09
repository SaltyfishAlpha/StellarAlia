#pragma once

/**
 * @file GraphicsContext.hpp
 * @brief Abstract graphics API interface
 *
 * This provides an abstraction layer for different graphics APIs (Vulkan,
 * OpenGL, DirectX, etc.). Implementations should inherit from this interface.
 */

#include "WindowSystem.hpp"

#include <cstdint>
#include <memory>

namespace StellarAlia::Function::Graphics {

    /**
     * @brief Graphics API types
     */
    enum class GraphicsAPI {
        None = 0,
        Vulkan,
        OpenGL,
        DirectX11,
        DirectX12,
        Metal
    };

    // Forward declaration
    namespace Window {
        class WindowSystem;
    }

    /**
     * @brief Graphics context creation parameters
     */
    struct GraphicsContextCreateInfo {
        GraphicsAPI api = GraphicsAPI::Vulkan;
        bool enableValidation = true;
        std::shared_ptr<WindowSystem> window = nullptr;  // Abstract window system interface
    };

    /**
     * @brief Abstract graphics context interface
     * 
     * This is the base class for all graphics API implementations.
     * Each graphics backend (Vulkan, OpenGL, etc.) should inherit from this.
     */
    class GraphicsContext {
    public:
        virtual ~GraphicsContext() = default;

        /**
         * @brief Initialize the graphics context
         * @param createInfo Creation parameters
         * @return True if initialization succeeded, false otherwise
         */
        virtual bool Initialize(const GraphicsContextCreateInfo& createInfo) = 0;

        /**
         * @brief Shutdown and cleanup the graphics context
         */
        virtual void Shutdown() = 0;

        /**
         * @brief Begin a frame (called at the start of each frame)
         */
        virtual void BeginFrame() = 0;

        /**
         * @brief End a frame (called at the end of each frame)
         */
        virtual void EndFrame() = 0;

        /**
         * @brief Present the rendered frame to the screen
         */
        virtual void Present() = 0;

        /**
         * @brief Wait for the GPU to finish all operations
         */
        virtual void WaitIdle() = 0;

        /**
         * @brief Get the graphics API type
         * @return The graphics API being used
         */
        virtual GraphicsAPI GetAPI() const = 0;

        /**
         * @brief Check if the context is initialized
         * @return True if initialized, false otherwise
         */
        virtual bool IsInitialized() const = 0;

        /**
         * @brief Get the swapchain width
         * @return Width in pixels
         */
        virtual uint32_t GetWidth() const = 0;

        /**
         * @brief Get the swapchain height
         * @return Height in pixels
         */
        virtual uint32_t GetHeight() const = 0;

        /**
         * @brief Resize the swapchain
         * @param width New width
         * @param height New height
         */
        virtual void Resize(uint32_t width, uint32_t height) = 0;

    protected:
        GraphicsContext() = default;
        GraphicsContext(const GraphicsContext&) = delete;
        GraphicsContext& operator=(const GraphicsContext&) = delete;
    };

    /**
     * @brief Create a graphics context based on the specified API
     * @param createInfo Creation parameters
     * @return Unique pointer to the created graphics context, or nullptr on failure
     */
    std::unique_ptr<GraphicsContext> CreateGraphicsContext(const GraphicsContextCreateInfo& createInfo);

} // namespace StellarAlia::Function::Graphics

