#pragma once

/**
 * @file RenderSystem.hpp
 * @brief High-level render system for managing graphics components
 * 
 * This system manages the graphics context, camera, scene, resources, and pipelines.
 * It provides a centralized interface for rendering operations and delegates
 * low-level graphics operations to the GraphicsContext.
 */

#include <cstdint>
#include <memory>
#include "function/graphics/GraphicsContext.hpp"

namespace StellarAlia::Function::Graphics
{
    // Forward declarations
    namespace Window
    {
        class WindowSystem; // WindowSystem is in nested namespace, so namespace syntax needed
    }

    // These will be high-level interfaces (to be implemented)
    class Camera;
    class Scene;
    class ResourceManager;
    class PipelineManager;

    /**
     * @brief Render system creation parameters
     * 
     * Window is assumed to be already initialized before RenderSystem initialization.
     */
    struct RenderSystemCreateInfo
    {
        GraphicsAPI api = GraphicsAPI::Vulkan;
        std::shared_ptr<WindowSystem> window = nullptr;
        const char* applicationName = "StellarAlia Application";
        bool enableValidation = true;
    };

    /**
     * @brief High-level render system
     * 
     * Centralized system for managing all graphics-related components including
     * the graphics context, camera, scene, resources, and pipelines.
     * This is a framework-agnostic interface that delegates low-level operations
     * to the GraphicsContext (which handles backend-specific implementations).
     */
    class RenderSystem
    {
    public:
        /**
         * @brief Constructor
         */
        RenderSystem();

        /**
         * @brief Destructor
         */
        ~RenderSystem();

        // Delete copy constructor and assignment operator
        RenderSystem(const RenderSystem&) = delete;
        RenderSystem& operator=(const RenderSystem&) = delete;

        /**
         * @brief Initialize the render system
         * @param createInfo Creation parameters
         * @return True if initialization succeeded, false otherwise
         */
        bool Initialize(const RenderSystemCreateInfo& createInfo);

        /**
         * @brief Shutdown and cleanup the render system
         */
        void Shutdown();

        /**
         * @brief Begin a frame (called at the start of each frame)
         * Delegates to GraphicsContext.
         */
        void BeginFrame();

        /**
         * @brief Render the current frame
         * Uses the camera, scene, resources, and pipelines to render.
         */
        void Render();

        /**
         * @brief End a frame (called at the end of each frame)
         * Delegates to GraphicsContext.
         */
        void EndFrame();

        /**
         * @brief Present the rendered frame to the screen
         * Delegates to GraphicsContext.
         */
        void Present();

        /**
         * @brief Wait for the GPU to finish all operations
         * Delegates to GraphicsContext.
         */
        void WaitIdle();

        /**
         * @brief Resize the render system (handles window resize events)
         * @param width New width
         * @param height New height
         * Delegates to GraphicsContext.
         */
        void Resize(uint32_t width, uint32_t height);

        // Component accessors

        /**
         * @brief Get the graphics context
         * @return Pointer to the graphics context, or nullptr if not initialized
         */
        GraphicsContext* GetGraphicsContext() const;

        /**
         * @brief Get the camera
         * @return Pointer to the camera, or nullptr if not set
         */
        Camera* GetCamera() const;

        /**
         * @brief Set the camera
         * @param camera Pointer to the camera to use
         */
        void SetCamera(Camera* camera);

        /**
         * @brief Get the scene
         * @return Pointer to the scene, or nullptr if not set
         */
        Scene* GetScene() const;

        /**
         * @brief Set the scene
         * @param scene Pointer to the scene to render
         */
        void SetScene(Scene* scene);

        /**
         * @brief Get the resource manager
         * @return Pointer to the resource manager, or nullptr if not initialized
         */
        ResourceManager* GetResourceManager() const;

        /**
         * @brief Get the pipeline manager
         * @return Pointer to the pipeline manager, or nullptr if not initialized
         */
        PipelineManager* GetPipelineManager() const;

        /**
         * @brief Get the graphics API type
         * @return The graphics API being used
         */
        GraphicsAPI GetAPI() const;

        /**
         * @brief Check if the render system is initialized
         * @return True if initialized, false otherwise
         */
        bool IsInitialized() const;

        /**
         * @brief Get the render width
         * @return Width in pixels (from graphics context)
         */
        uint32_t GetWidth() const;

        /**
         * @brief Get the render height
         * @return Height in pixels (from graphics context)
         */
        uint32_t GetHeight() const;

    private:
        std::unique_ptr<GraphicsContext> m_graphicsContext;

        Camera* m_camera = nullptr;
        Scene* m_scene = nullptr;
        ResourceManager* m_resourceManager = nullptr;
        PipelineManager* m_pipelineManager = nullptr;

        bool m_initialized = false;
        GraphicsAPI m_api = GraphicsAPI::None;
    };
} // namespace StellarAlia::Function::Graphics
