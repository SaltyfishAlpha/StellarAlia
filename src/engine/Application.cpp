#include "engine/Application.hpp"

#include "core/logs/Log.hpp"
#include "core/Profiler.hpp"
#include "function/scene/Components.hpp"
#include "platform/input/GLFWInputProvider.hpp"
#include "platform/rhi/vulkan/VulkanDevice.hpp"
#include "platform/window/GLFWWindow.hpp"

#include <filesystem>
#include <chrono>

namespace fs = std::filesystem;
using namespace StellarAlia::RHI;
using namespace StellarAlia::Platform;

namespace StellarAlia {

Application::Application(std::unique_ptr<AppMode> mode)
    : m_mode(std::move(mode))
    , m_scene(nullptr)
{}

Application::~Application() {
    if (m_initialized)
        Shutdown();
}

bool Application::Init(const Desc& desc) {
    m_desc = desc;
    Core::Log::Initialize();
    SA_LOG_INFO("Application: initialising");

    if (!desc.engineCookCacheDir.empty())
        fs::create_directories(desc.engineCookCacheDir);
    if (!desc.cookCacheDir.empty())
        fs::create_directories(desc.cookCacheDir);

    // ── Window ────────────────────────────────────────────────────────────────
    m_window = GLFWWindow::Create(WindowDesc{
        desc.width, desc.height, desc.title
    });
    if (!m_window) {
        SA_LOG_CRITICAL("Application: window creation failed");
        return false;
    }

    // ── Input provider ────────────────────────────────────────────────────────
    m_provider = std::make_unique<GLFWInputProvider>(
        static_cast<GLFWwindow*>(m_window->GetNativeHandle())
    );
    m_input.Init(m_provider.get());

    // ── Vulkan device ─────────────────────────────────────────────────────────
    RHIDeviceDesc devDesc{};
    devDesc.windowHandle     = NativeWindowHandle{ m_window->GetNativeHandle() };
    devDesc.swapchainWidth   = m_window->GetWidth();
    devDesc.swapchainHeight  = m_window->GetHeight();
    devDesc.vsync            = desc.vsync;
    devDesc.enableValidation = desc.validation;
    m_device = VulkanDevice::Create(devDesc);
    if (!m_device) {
        SA_LOG_CRITICAL("Application: Vulkan device creation failed");
        return false;
    }

    // ── Resource / Material managers ──────────────────────────────────────────
    m_resMgr.Init(desc.engineCookCacheDir, m_device.get());
    if (!desc.cookCacheDir.empty())
        m_resMgr.SetProjectCookCache(desc.cookCacheDir);
    m_matMgr.Init(m_device.get(), &m_resMgr);

    // ── SceneRenderer ─────────────────────────────────────────────────────────
    SceneRenderer::Desc rendDesc{};
    rendDesc.device       = m_device.get();
    rendDesc.matMgr       = &m_matMgr;
    rendDesc.resMgr       = &m_resMgr;
    rendDesc.shaderDir    = desc.shaderDir;
    // IBL bake output goes to the project cache when loaded, engine cache otherwise.
    rendDesc.cookCacheDir = desc.cookCacheDir.empty() ? desc.engineCookCacheDir : desc.cookCacheDir;
    if (!m_renderer.Init(rendDesc)) {
        SA_LOG_CRITICAL("Application: renderer init failed");
        return false;
    }
    m_renderer.SetDebugDraw(&m_debugDraw);
    m_animSystem.Init(m_device.get(), m_renderer.GetSkinDescLayout());

    // ── PhysicsSystem ─────────────────────────────────────────────────────────
    if (!m_physics.Init(&m_debugDraw))
        SA_LOG_WARN("Application: PhysicsSystem init failed — physics unavailable");

    // ── Scene ─────────────────────────────────────────────────────────────────
    m_scene = std::make_unique<Scene>("Main");

    // ── Hand off to mode ──────────────────────────────────────────────────────
    m_mode->OnAttach(*this);

    // Prepare any skinned entities the mode placed in the scene so they are
    // visible at bind pose (frame 0) even before Play mode is entered.
    PrepareAnimatedEntities();

    // Build the initial draw list after the mode has populated the scene.
    m_scene->UpdateTransforms();
    m_renderer.BuildDrawList(*m_scene);

    m_initialized = true;
    SA_LOG_INFO("Application: ready");
    return true;
}

void Application::Run() {
    using Clock = std::chrono::steady_clock;
    auto lastTime = Clock::now();

    while (!m_window->ShouldClose()) {
        SA_PROFILE_SCOPE_N("Frame");

        const auto  now = Clock::now();
        const float dt  = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        // ── Input ─────────────────────────────────────────────────────────────
        {
            SA_PROFILE_SCOPE_N("Input");
            m_window->PollEvents();
            m_input.Poll();
        }

        // ── Debug draw — clear before mode populates it ───────────────────────
        m_debugDraw.Clear();

        // ── Fixed-step physics (only while simulation is running) ─────────────
        {
            SA_PROFILE_SCOPE_N("Physics");
            constexpr float kFixedStep = 1.f / 60.f;
            if (m_playState == EnginePlayState::Playing) {
                m_physicsAccumulator += dt;
                while (m_physicsAccumulator >= kFixedStep) {
                    m_physics.SyncIn(*m_scene);
                    m_physics.Step(kFixedStep);
                    m_physics.SyncOut(*m_scene);
                    m_physicsAccumulator -= kFixedStep;
                }
            }
            m_physics.DrawDebug(m_physicsDebugSettings, *m_scene);
        }

        // ── Mode update ───────────────────────────────────────────────────────
        {
            SA_PROFILE_SCOPE_N("ModeUpdate");
            m_mode->OnUpdate(dt);
        }

        // ── Animation (only while simulation is running) ──────────────────────
        if (m_playState == EnginePlayState::Playing) {
            SA_PROFILE_SCOPE_N("Animation");
            m_animSystem.Update(dt, m_scene->Registry(), m_resMgr, m_device.get());
        }

        // ── Editor skinned-mesh refresh: inspector changed meshAsset → re-prepare
        //    (SceneRenderer handles static-mesh rebuild; skinned meshes also need
        //    AnimationSystem::PrepareEntity which only Application can call) ───────
        if (m_playState == EnginePlayState::Editing && m_scene->IsAndClearSkinnedMeshDirty()) {
            PrepareAnimatedEntities();
            RebuildDrawList();
        }

        // ── Render ────────────────────────────────────────────────────────────
        // Skip the frame while the window is minimized (GLFW framebuffer size = 0x0).
        // m_device->GetSwapchainWidth/Height() lags behind because ResizeSwapchain()
        // is never called with 0x0; we check the window directly instead.
        if (m_window->GetWidth() == 0 || m_window->GetHeight() == 0) continue;
        const auto w = m_device->GetSwapchainWidth();
        const auto h = m_device->GetSwapchainHeight();
        if (w == 0 || h == 0) continue;

        const float aspect = static_cast<float>(w) / static_cast<float>(h);
        m_scene->UpdateTransforms();
        m_renderer.RenderFrame(*m_scene, m_mode->GetCameraData(aspect), w, h,
            [this](RHI::IRHICommandList* cmd) { m_mode->OnRenderUI(cmd); });

        SA_PROFILE_FRAME();
    }
}

void Application::Shutdown() {
    if (!m_initialized) return;
    m_initialized = false;

    SA_LOG_INFO("Application: shutting down");
    m_device->WaitIdle();

    m_mode->OnDetach();

    m_physics.Shutdown();
    m_animSystem.Shutdown(m_device.get(), m_scene->Registry());
    m_renderer.Shutdown();
    m_input.Shutdown();
    m_matMgr.Shutdown();
    m_resMgr.Shutdown();

    m_scene.reset();
    m_provider.reset();
    m_device.reset();
    m_window.reset();

    Core::Log::Shutdown();
}

Platform::GLFWInputProvider& Application::GetInputProvider() {
    return *m_provider;
}

RHI::VulkanDevice& Application::GetVulkanDevice() {
    return static_cast<RHI::VulkanDevice&>(*m_device);
}

void* Application::GetNativeWindow() {
    return m_window->GetNativeHandle();
}

void Application::RebuildDrawList() {
    m_scene->UpdateTransforms();
    m_renderer.BuildDrawList(*m_scene);
}

void Application::PrepareAnimatedEntities() {
    auto& reg = m_scene->Registry();
    for (auto entity : reg.view<SkinnedMeshComponent>())
        m_animSystem.PrepareEntity(entity, reg, m_resMgr, m_device.get());
    // Populate lastGlobalPose at t=0 so the skeleton gizmo is visible in editor mode.
    m_animSystem.EvaluateAll(0.f, reg, m_resMgr, m_device.get());
}

void Application::SetPlayState(EnginePlayState newState) {
    if (newState == m_playState) return;
    const EnginePlayState old = m_playState;

    if (newState == EnginePlayState::Playing) {
        if (old == EnginePlayState::Editing) {
            // Fresh start — reset all clip times, re-prepare, rebuild draw list.
            m_scene->Registry().view<AnimatorComponent>().each(
                [](AnimatorComponent& a) { a.time = 0.f; });
            PrepareAnimatedEntities();
            RebuildDrawList();
            SA_LOG_INFO("Application: Play");
        } else {
            // Resume from Pause — preserve current frame.
            SA_LOG_INFO("Application: Resume");
        }
    } else if (newState == EnginePlayState::Paused) {
        // Freeze at the current deformed frame; GPU buffer unchanged.
        SA_LOG_INFO("Application: Paused");
    } else {
        // Stop → Editing: destroy all Jolt bodies so bodyIds are cleared, then
        // restore WorldTransformComponent from TransformComponent before the next
        // render frame — otherwise physics-driven positions persist in the editor.
        m_physics.Reset(*m_scene);
        m_physicsAccumulator = 0.f;

        // Reset animation to frame 0.
        m_scene->Registry().view<AnimatorComponent>().each(
            [](AnimatorComponent& a) { a.time = 0.f; });
        m_animSystem.EvaluateAll(0.f, m_scene->Registry(), m_resMgr, m_device.get());

        // Recompute world transforms and rebuild the draw list with restored poses.
        RebuildDrawList();
        SA_LOG_INFO("Application: Stop → Edit");
    }

    m_playState = newState;
    m_mode->OnPlayStateChanged(m_playState);
}

void Application::UpdateProjectPaths(const std::filesystem::path& projectDir,
                                      const std::filesystem::path& cookCacheDir) {
    m_desc.projectDir   = projectDir.string();
    m_desc.cookCacheDir = cookCacheDir.string();
    if (!cookCacheDir.empty())
        fs::create_directories(cookCacheDir);
    m_resMgr.SetProjectCookCache(cookCacheDir);
    m_renderer.SetCookCacheDir(cookCacheDir.string());
}

} // namespace StellarAlia
