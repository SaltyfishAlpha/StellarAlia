#include "core/logs/Log.hpp"
#include "platform/window/GLFWWindow.hpp"
#include "platform/rhi/vulkan/VulkanDevice.hpp"

using namespace StellarAlia;
using namespace StellarAlia::RHI;
using namespace StellarAlia::Platform;

int main() {
    Core::Log::Initialize();
    SA_LOG_INFO("=== StellarAlia Sandbox ===");

    // ── Window ────────────────────────────────────────────────────────────────
    auto window = GLFWWindow::Create({
        .width    = 1280,
        .height   = 720,
        .title    = "StellarAlia — Sandbox",
        .resizable = true,
    });
    if (!window) {
        SA_LOG_CRITICAL("Failed to create window");
        return 1;
    }

    // ── Vulkan Device ─────────────────────────────────────────────────────────
    auto device = VulkanDevice::Create({
        .windowHandle      = {window->GetNativeHandle()},
        .swapchainWidth    = window->GetWidth(),
        .swapchainHeight   = window->GetHeight(),
        .swapchainImageCount = 2,
        .vsync             = true,
        .enableValidation  = true,
    });
    if (!device) {
        SA_LOG_CRITICAL("Failed to create VulkanDevice");
        return 1;
    }

    SA_LOG_INFO("Swapchain: {}x{}  format={}",
                device->GetSwapchainWidth(),
                device->GetSwapchainHeight(),
                static_cast<uint32_t>(device->GetSwapchainFormat()));

    // ── Main Loop ─────────────────────────────────────────────────────────────
    while (!window->ShouldClose()) {
        window->PollEvents();

        // Notify device of window resize (GLFW resize is handled via callback
        // updating IWindow dimensions; here we propagate to the swapchain).
        static uint32_t lastW = window->GetWidth(), lastH = window->GetHeight();
        if (window->GetWidth() != lastW || window->GetHeight() != lastH) {
            lastW = window->GetWidth();
            lastH = window->GetHeight();
            device->ResizeSwapchain(lastW, lastH);
        }

        // BeginFrame: acquires swapchain image, clears to background colour.
        // Returns nullptr if the swapchain was just recreated — skip the frame.
        IRHICommandList* cmd = device->BeginFrame();
        if (!cmd) continue;

        // Nothing to record yet — the background clear is handled inside BeginFrame.
        // Future stages will record geometry + lighting passes here via RenderGraph.

        device->EndFrame();
        device->Present();
    }

    // ── Shutdown ──────────────────────────────────────────────────────────────
    // Explicitly destroy GPU objects first so their destructors can still log.
    device->WaitIdle();
    device.reset();
    window.reset();
    SA_LOG_INFO("Sandbox: clean shutdown");
    Core::Log::Shutdown();
    return 0;
}
