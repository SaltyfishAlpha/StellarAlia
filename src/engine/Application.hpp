#pragma once

#include "engine/AppMode.hpp"
#include "function/input/InputSystem.hpp"
#include "function/material/MaterialManager.hpp"
#include "function/renderer/SceneRenderer.hpp"
#include "function/scene/Scene.hpp"
#include "resource/ResourceManager.hpp"

#include <memory>
#include <string>

// Forward declarations — implementations are GLFW/Vulkan specific, kept in Application.cpp
struct GLFWwindow;
namespace StellarAlia::Platform { class GLFWWindow; class GLFWInputProvider; }
namespace StellarAlia::RHI     { class IRHIDevice; class VulkanDevice; }

namespace StellarAlia {

// ─────────────────────────────────────────────────────────────────────────────
// Application — top-level engine owner.
//
// Owns all core systems (Window, Device, Input, Resources, Renderer, Scene)
// and delegates per-frame logic to the active AppMode.
//
// Usage:
//   Application app(std::make_unique<EditorMode>());
//   if (!app.Init(desc)) return 1;
//   app.Run();
//   app.Shutdown();
// ─────────────────────────────────────────────────────────────────────────────
class Application {
public:
    struct Desc {
        uint32_t    width        = 1280;
        uint32_t    height       = 720;
        const char* title        = "StellarAlia";
        bool        vsync        = true;
        bool        validation   = false;   // Vulkan validation layers
        std::string assetsDir;       // path to assets/ (scenes, textures, models…)
        std::string cookCacheDir;
        std::string shaderDir;
    };

    explicit Application(std::unique_ptr<AppMode> mode);
    ~Application();

    // Non-copyable
    Application(const Application&)            = delete;
    Application& operator=(const Application&) = delete;

    // Initialise all systems and call mode->OnAttach. Returns false on failure.
    bool Init(const Desc& desc);

    // Run the main loop until the window is closed.
    void Run();

    // Destroy all systems in reverse order and call mode->OnDetach.
    void Shutdown();

    // ── System accessors (valid after Init, before Shutdown) ─────────────────
    Scene&                       GetScene()            { return *m_scene; }
    SceneRenderer&               GetRenderer()         { return m_renderer; }
    InputSystem&                 GetInputSystem()      { return m_input; }
    Platform::GLFWInputProvider& GetInputProvider();
    Resource::ResourceManager&   GetResourceManager()  { return m_resMgr; }
    // Returns the concrete Vulkan device. Use only in the editor layer.
    RHI::VulkanDevice&           GetVulkanDevice();
    // Returns the native GLFWwindow* (as void* to avoid leaking GLFW headers).
    // Cast to GLFWwindow* at the call site after including GLFW.
    void*                        GetNativeWindow();
    const Desc&                  GetDesc()       const { return m_desc; }

    // Call after changing scene content at runtime (e.g. loading a new level).
    void RebuildDrawList();

private:
    Desc                                           m_desc;
    std::unique_ptr<AppMode>                      m_mode;
    std::unique_ptr<Platform::GLFWWindow>          m_window;
    std::unique_ptr<RHI::IRHIDevice>               m_device;
    std::unique_ptr<Platform::GLFWInputProvider>   m_provider;
    std::unique_ptr<Scene>                         m_scene;

    Resource::ResourceManager  m_resMgr;
    MaterialManager            m_matMgr;
    SceneRenderer              m_renderer;
    InputSystem                m_input;

    bool m_initialized = false;
};

} // namespace StellarAlia
