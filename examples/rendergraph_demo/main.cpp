#include "core/logs/Log.hpp"
#include "platform/window/GLFWWindow.hpp"
#include "platform/rhi/vulkan/VulkanDevice.hpp"
#include "function/render_graph/RenderGraph.hpp"

using namespace StellarAlia;
using namespace StellarAlia::RHI;
using namespace StellarAlia::Platform;

int main() {
    Core::Log::Initialize();
    SA_LOG_INFO("=== RenderGraph Demo ===");

    // ── Window ────────────────────────────────────────────────────────────────
    auto window = GLFWWindow::Create({
        .width     = 1280,
        .height    = 720,
        .title     = "StellarAlia — RenderGraph Demo",
        .resizable = true,
    });
    if (!window) { SA_LOG_CRITICAL("Failed to create window"); return 1; }

    // ── Vulkan Device ─────────────────────────────────────────────────────────
    auto device = VulkanDevice::Create({
        .windowHandle        = {window->GetNativeHandle()},
        .swapchainWidth      = window->GetWidth(),
        .swapchainHeight     = window->GetHeight(),
        .swapchainImageCount = 2,
        .vsync               = true,
        .enableValidation    = true,
    });
    if (!device) { SA_LOG_CRITICAL("Failed to create VulkanDevice"); return 1; }

    SA_LOG_INFO("Swapchain: {}x{}  format={}",
                device->GetSwapchainWidth(),
                device->GetSwapchainHeight(),
                static_cast<uint32_t>(device->GetSwapchainFormat()));

    RenderGraph rg;
    uint32_t frameCount = 0;
    uint32_t lastW = window->GetWidth(), lastH = window->GetHeight();

    while (!window->ShouldClose()) {
        window->PollEvents();

        if (window->GetWidth() != lastW || window->GetHeight() != lastH) {
            lastW = window->GetWidth();
            lastH = window->GetHeight();
            device->ResizeSwapchain(lastW, lastH);
        }

        IRHICommandList* cmd = device->BeginFrame();
        if (!cmd) continue;

        const uint32_t W = device->GetSwapchainWidth();
        const uint32_t H = device->GetSwapchainHeight();

        // ── Build RenderGraph ─────────────────────────────────────────────────
        rg.Reset();

        // ── Texture declarations ──────────────────────────────────────────────
        //
        // Dependency graph (arrows = "must execute before"):
        //
        //   GBufferPass ──┐
        //                 ├──► LightingPass ──► PostFXPass ──► swapchain
        //   ShadowPass ───┘
        //
        // Transient textures have no real GPU backing yet (Stage 3 will add that).
        // The RG still tracks their state transitions and sorts passes correctly.

        auto rgSwapchain = rg.ImportTexture(
            "swapchain",
            device->GetSwapchainTexture(),
            RHIResourceState::RenderTarget,   // BeginFrame left it here
            RHIResourceState::RenderTarget);  // EndFrame will handle → PresentSrc

        RHITextureDesc hdrDesc{};
        hdrDesc.width  = W;
        hdrDesc.height = H;
        hdrDesc.format = RHIFormat::RGBA16F;
        hdrDesc.usage  = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled;
        auto rgHDR = rg.CreateTexture("HDRBuffer", hdrDesc);

        RHITextureDesc gbufDesc{};
        gbufDesc.width  = W;
        gbufDesc.height = H;
        gbufDesc.format = RHIFormat::RGBA16F;
        gbufDesc.usage  = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled;
        auto rgGBuffer = rg.CreateTexture("GBuffer", gbufDesc);

        RHITextureDesc shadowDesc{};
        shadowDesc.width  = 2048;
        shadowDesc.height = 2048;
        shadowDesc.format = RHIFormat::D32F;
        shadowDesc.usage  = RHITextureUsage::DepthStencil | RHITextureUsage::Sampled;
        auto rgShadowMap = rg.CreateTexture("ShadowMap", shadowDesc);

        // ── Pass declarations — intentionally in REVERSE dependency order ─────
        // Expected sorted order: GBufferPass + ShadowPass → LightingPass → PostFXPass
        // If the sorter is broken the log will show them running in declaration order.

        // 1. PostFXPass: reads HDRBuffer, writes swapchain (tonemap / bloom / etc.)
        rg.AddPass("PostFXPass",
            [&](RGPassBuilder& b) {
                b.Read(rgHDR);
                b.Write(rgSwapchain);
            },
            [&](IRHICommandList& c, const RGResources& res) {
                if (frameCount == 0)
                    SA_LOG_INFO("  [frame 0] execute: PostFXPass");

                // PostFXPass is the only pass with a real swapchain attachment.
                c.SetViewport({0.f, 0.f, float(W), float(H), 0.f, 1.f});
                c.SetScissor({0, 0, W, H});

                RHIRenderPassDesc rp{};
                rp.colorAttachments[0] = {
                    .texture    = res.Get(rgSwapchain),
                    .clearOnLoad = false,   // BeginFrame already cleared
                };
                rp.colorAttachmentCount = 1;
                rp.width  = W;
                rp.height = H;
                c.BeginRenderPass(rp);
                // Tonemapping draw calls go here in Stage 3+.
                c.EndRenderPass();
            });

        // 2. LightingPass: reads GBuffer + ShadowMap, writes HDRBuffer
        rg.AddPass("LightingPass",
            [&](RGPassBuilder& b) {
                b.Read(rgGBuffer);
                b.Read(rgShadowMap);
                b.Write(rgHDR);
            },
            [&](IRHICommandList& /*c*/, const RGResources& /*res*/) {
                if (frameCount == 0)
                    SA_LOG_INFO("  [frame 0] execute: LightingPass");
                // Deferred lighting draw calls go here in Stage 3+.
            });

        // 3. GBufferPass: writes GBuffer (albedo / normals / material)
        rg.AddPass("GBufferPass",
            [&](RGPassBuilder& b) {
                b.Write(rgGBuffer);
            },
            [&](IRHICommandList& /*c*/, const RGResources& /*res*/) {
                if (frameCount == 0)
                    SA_LOG_INFO("  [frame 0] execute: GBufferPass");
                // Geometry draw calls go here in Stage 3+.
            });

        // 4. ShadowPass: writes ShadowMap
        rg.AddPass("ShadowPass",
            [&](RGPassBuilder& b) {
                b.WriteDepth(rgShadowMap);
            },
            [&](IRHICommandList& /*c*/, const RGResources& /*res*/) {
                if (frameCount == 0)
                    SA_LOG_INFO("  [frame 0] execute: ShadowPass");
                // Shadow caster draw calls go here in Stage 3+.
            });

        if (frameCount == 0)
            SA_LOG_INFO("Pass declaration order: PostFXPass, LightingPass, GBufferPass, ShadowPass");

        rg.Compile();

        if (frameCount == 0)
            SA_LOG_INFO("Expected execution order: GBufferPass + ShadowPass -> LightingPass -> PostFXPass");

        rg.Execute(*device, *cmd);

        device->EndFrame();
        device->Present();
        ++frameCount;
    }

    // ── Shutdown ──────────────────────────────────────────────────────────────
    device->WaitIdle();
    device.reset();
    window.reset();
    SA_LOG_INFO("RenderGraph Demo: clean shutdown ({} frames)", frameCount);
    Core::Log::Shutdown();
    return 0;
}
