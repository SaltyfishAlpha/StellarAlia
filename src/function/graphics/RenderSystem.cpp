#include "function/graphics/RenderSystem.hpp"
#include "function/graphics/GraphicsContext.hpp"
#include "function/graphics/WindowSystem.hpp"

namespace StellarAlia::Function::Graphics {

    RenderSystem::RenderSystem() = default;

    RenderSystem::~RenderSystem() {
        Shutdown();
    }

    bool RenderSystem::Initialize(const RenderSystemCreateInfo& createInfo) {
        if (m_initialized) {
            return false; // Already initialized
        }

        if (!createInfo.window) {
            return false; // Window must be provided and already initialized
        }

        // Get window dimensions (window is already initialized)
        uint32_t width = createInfo.window->GetWidth();
        uint32_t height = createInfo.window->GetHeight();

        // Create graphics context
        GraphicsContextCreateInfo contextInfo;
        contextInfo.api = createInfo.api;
        contextInfo.enableValidation = createInfo.enableValidation;
        contextInfo.window = createInfo.window; // Pass raw pointer to GraphicsContext

        m_graphicsContext = CreateGraphicsContext(contextInfo);
        if (!m_graphicsContext) {
            return false;
        }

        if (!m_graphicsContext->Initialize(contextInfo)) {
            m_graphicsContext.reset();
            return false;
        }

        m_api = createInfo.api;
        m_initialized = true;

        // TODO: Initialize ResourceManager and PipelineManager when implemented
        // m_resourceManager = CreateResourceManager(...);
        // m_pipelineManager = CreatePipelineManager(...);

        return true;
    }

    void RenderSystem::Shutdown() {
        if (!m_initialized) {
            return;
        }

        // TODO: Cleanup ResourceManager and PipelineManager when implemented

        if (m_graphicsContext) {
            m_graphicsContext->Shutdown();
            m_graphicsContext.reset();
        }

        m_camera = nullptr;
        m_scene = nullptr;
        m_resourceManager = nullptr;
        m_pipelineManager = nullptr;
        m_initialized = false;
        m_api = GraphicsAPI::None;
    }

    void RenderSystem::BeginFrame() {
        if (!m_initialized || !m_graphicsContext) {
            return;
        }
        m_graphicsContext->BeginFrame();
    }

    void RenderSystem::Render() {
        if (!m_initialized || !m_graphicsContext) {
            return;
        }

        // TODO: Implement rendering logic using camera, scene, resources, and pipelines
        // This will be implemented when Camera, Scene, ResourceManager, and PipelineManager are created
    }

    void RenderSystem::EndFrame() {
        if (!m_initialized || !m_graphicsContext) {
            return;
        }
        m_graphicsContext->EndFrame();
    }

    void RenderSystem::Present() {
        if (!m_initialized || !m_graphicsContext) {
            return;
        }
        m_graphicsContext->Present();
    }

    void RenderSystem::WaitIdle() {
        if (!m_initialized || !m_graphicsContext) {
            return;
        }
        m_graphicsContext->WaitIdle();
    }

    void RenderSystem::Resize(uint32_t width, uint32_t height) {
        if (!m_initialized || !m_graphicsContext) {
            return;
        }
        m_graphicsContext->Resize(width, height);
    }

    GraphicsContext* RenderSystem::GetGraphicsContext() const {
        return m_graphicsContext.get();
    }

    Camera* RenderSystem::GetCamera() const {
        return m_camera;
    }

    void RenderSystem::SetCamera(Camera* camera) {
        m_camera = camera;
    }

    Scene* RenderSystem::GetScene() const {
        return m_scene;
    }

    void RenderSystem::SetScene(Scene* scene) {
        m_scene = scene;
    }

    ResourceManager* RenderSystem::GetResourceManager() const {
        return m_resourceManager;
    }

    PipelineManager* RenderSystem::GetPipelineManager() const {
        return m_pipelineManager;
    }

    GraphicsAPI RenderSystem::GetAPI() const {
        return m_api;
    }

    bool RenderSystem::IsInitialized() const {
        return m_initialized;
    }

    uint32_t RenderSystem::GetWidth() const {
        if (!m_graphicsContext) {
            return 0;
        }
        return m_graphicsContext->GetWidth();
    }

    uint32_t RenderSystem::GetHeight() const {
        if (!m_graphicsContext) {
            return 0;
        }
        return m_graphicsContext->GetHeight();
    }

} // namespace StellarAlia::Function::Graphics

