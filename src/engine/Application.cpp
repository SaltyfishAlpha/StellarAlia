#include "engine/Application.hpp"

#include "core/logs/Log.hpp"
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
    m_resMgr.Init(desc.cookCacheDir, m_device.get());
    m_matMgr.Init(m_device.get(), &m_resMgr);

    // ── SceneRenderer ─────────────────────────────────────────────────────────
    SceneRenderer::Desc rendDesc{};
    rendDesc.device       = m_device.get();
    rendDesc.matMgr       = &m_matMgr;
    rendDesc.resMgr       = &m_resMgr;
    rendDesc.shaderDir    = desc.shaderDir;
    rendDesc.cookCacheDir = desc.cookCacheDir;
    if (!m_renderer.Init(rendDesc)) {
        SA_LOG_CRITICAL("Application: renderer init failed");
        return false;
    }

    // ── Scene ─────────────────────────────────────────────────────────────────
    m_scene = std::make_unique<Scene>("Main");

    // ── Hand off to mode ──────────────────────────────────────────────────────
    m_mode->OnAttach(*this);

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
        const auto  now = Clock::now();
        const float dt  = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        // ── Input ─────────────────────────────────────────────────────────────
        m_window->PollEvents();
        m_input.Poll();

        // ── Mode update ───────────────────────────────────────────────────────
        m_mode->OnUpdate(dt);

        // ── Render ────────────────────────────────────────────────────────────
        const auto w = m_device->GetSwapchainWidth();
        const auto h = m_device->GetSwapchainHeight();
        if (w == 0 || h == 0) continue;

        const float aspect = static_cast<float>(w) / static_cast<float>(h);
        m_scene->UpdateTransforms();
        m_renderer.RenderFrame(*m_scene, m_mode->GetCameraData(aspect), w, h,
            [this](RHI::IRHICommandList* cmd) { m_mode->OnRenderUI(cmd); });
    }
}

void Application::Shutdown() {
    if (!m_initialized) return;
    m_initialized = false;

    SA_LOG_INFO("Application: shutting down");
    m_device->WaitIdle();

    m_mode->OnDetach();

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

} // namespace StellarAlia
