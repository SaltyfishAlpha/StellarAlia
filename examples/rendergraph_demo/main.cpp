#include "core/logs/Log.hpp"
#include "platform/window/GLFWWindow.hpp"
#include "platform/rhi/vulkan/VulkanDevice.hpp"
#include "function/render_graph/RenderGraph.hpp"

using namespace StellarAlia;
using namespace StellarAlia::RHI;
using namespace StellarAlia::Platform;

// Print aliasing stats to the log once on the first frame and whenever
// physicalSlotCount changes (e.g. after a viewport resize rebuilds the pool).
static void PrintStats(const RGStats& s, uint32_t& lastPhysical) {
    if (s.physicalSlotCount == lastPhysical && lastPhysical != 0) return;
    lastPhysical = s.physicalSlotCount;

    constexpr double kMB = 1.0 / (1024.0 * 1024.0);
    const double logMB  = static_cast<double>(s.transientBytesLogical)  * kMB;
    const double physMB = static_cast<double>(s.transientBytesPhysical) * kMB;

    SA_LOG_INFO("── RGStats ───────────────────────────────────────");
    SA_LOG_INFO("  Transient : {} logical  /  {} physical slots",
                s.transientCount, s.physicalSlotCount);
    SA_LOG_INFO("  Imported  : {}", s.importedCount);
    SA_LOG_INFO("  Logical   : {:.2f} MB", logMB);
    if (s.physicalSlotCount < s.transientCount) {
        const double saved    = logMB - physMB;
        const double savedPct = saved / logMB * 100.0;
        SA_LOG_INFO("  Physical  : {:.2f} MB  (saved {:.2f} MB = {:.1f}%  ← aliasing!)",
                    physMB, saved, savedPct);
    } else {
        SA_LOG_INFO("  Physical  : {:.2f} MB", physMB);
    }
    for (const auto& e : s.entries) {
        SA_LOG_INFO("    {:20s}  {}x{}  {}  {:.2f} MB  slot={}",
                    e.name, e.width, e.height, e.formatStr ? e.formatStr : "?",
                    static_cast<double>(e.bytes) * kMB,
                    e.slotIndex);
    }
    SA_LOG_INFO("──────────────────────────────────────────────────");
}

int main() {
    Core::Log::Initialize();
    SA_LOG_INFO("=== RenderGraph Aliasing Demo ===");

    // ── Window ────────────────────────────────────────────────────────────────
    auto window = GLFWWindow::Create({
        .width     = 1280,
        .height    = 720,
        .title     = "StellarAlia — RenderGraph Aliasing Demo",
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

    RenderGraph rg;
    uint32_t frameCount  = 0;
    uint32_t lastPhysical = 0;
    uint32_t lastW = window->GetWidth(), lastH = window->GetHeight();

    while (!window->ShouldClose()) {
        window->PollEvents();

        if (window->GetWidth() != lastW || window->GetHeight() != lastH) {
            device->WaitIdle();
            lastW = window->GetWidth();
            lastH = window->GetHeight();
            device->ResizeSwapchain(lastW, lastH);
            // Slot GPU textures have the old size — destroy and let Compile
            // rebuild the pool with the new viewport dimensions next frame.
            rg.InvalidateSlots(*device);
        }

        IRHICommandList* cmd = device->BeginFrame();
        if (!cmd) continue;

        const uint32_t W = device->GetSwapchainWidth();
        const uint32_t H = device->GetSwapchainHeight();

        // ── Build RenderGraph ─────────────────────────────────────────────────
        rg.Reset();

        // ── Texture declarations ──────────────────────────────────────────────
        //
        // Pipeline (arrows = execution order):
        //
        //   GBufferPass ─────┐
        //                    ├──► LightingPass ──► PostFXPass ──► FinalPass ──► swapchain
        //   ShadowPass ──────┘
        //
        // Transient texture lifetimes (si = sorted-pass index):
        //
        //   GBuffer    [RGBA16F W×H]   : si 0 (write GBuf)  … si 2 (read Lighting)
        //   ShadowMap  [D32F   2048²]  : si 1 (write Shad)  … si 2 (read Lighting)
        //   HDRBuffer  [RGBA16F W×H]   : si 2 (write Light) … si 3 (read PostFX)
        //   Scratch    [RGBA16F W×H]   : si 3 (write PostFX)… si 4 (read Final)
        //
        // Greedy slot assignment (GBuffer freed at si=2, Scratch starts si=3 → alias):
        //   slot 0  [RGBA16F W×H]  ← GBuffer then Scratch  ← ALIAS
        //   slot 1  [D32F   2048²] ← ShadowMap
        //   slot 2  [RGBA16F W×H]  ← HDRBuffer
        //
        // Expected: transientCount=4, physicalSlotCount=3, ~37% savings

        auto rgSwapchain = rg.ImportTexture(
            "Swapchain",
            device->GetSwapchainTexture(),
            RHIResourceState::RenderTarget,
            RHIResourceState::RenderTarget);

        RHITextureDesc rgbaDesc{};
        rgbaDesc.width  = W;
        rgbaDesc.height = H;
        rgbaDesc.format = RHIFormat::RGBA16F;
        rgbaDesc.usage  = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled;

        RHITextureDesc depthDesc{};
        depthDesc.width  = 2048;
        depthDesc.height = 2048;
        depthDesc.format = RHIFormat::D32F;
        depthDesc.usage  = RHITextureUsage::DepthStencil | RHITextureUsage::Sampled;

        auto rgGBuffer   = rg.CreateTexture("GBuffer",       rgbaDesc);
        auto rgShadowMap = rg.CreateTexture("ShadowMap",     depthDesc);
        auto rgHDR       = rg.CreateTexture("HDRBuffer",     rgbaDesc);
        auto rgScratch   = rg.CreateTexture("PostFX_Scratch", rgbaDesc);
        // rgGBuffer and rgScratch: same desc, non-overlapping lifetimes → alias on slot 0.

        // ── Pass declarations (forward dependency order) ──────────────────────

        // GBufferPass: writes GBuffer
        rg.AddPass("GBufferPass",
            [rgGBuffer](RGPassBuilder& b) { b.Write(rgGBuffer); },
            [rgGBuffer, W, H](IRHICommandList& cmd, const RGResources& res) {
                RHIRenderPassDesc rp{};
                rp.colorAttachments[0] = { .texture = res.Get(rgGBuffer), .clearOnLoad = true };
                rp.colorAttachmentCount = 1;
                rp.width = W; rp.height = H;
                cmd.BeginRenderPass(rp);
                cmd.EndRenderPass();
            });

        // ShadowPass: writes ShadowMap (depth)
        rg.AddPass("ShadowPass",
            [rgShadowMap](RGPassBuilder& b) { b.WriteDepth(rgShadowMap); },
            [](IRHICommandList& /*cmd*/, const RGResources& /*res*/) {});

        // LightingPass: reads GBuffer + ShadowMap, writes HDRBuffer
        rg.AddPass("LightingPass",
            [rgGBuffer, rgShadowMap, rgHDR](RGPassBuilder& b) {
                b.Read(rgGBuffer);
                b.Read(rgShadowMap);
                b.Write(rgHDR);
            },
            [rgHDR, W, H](IRHICommandList& cmd, const RGResources& res) {
                RHIRenderPassDesc rp{};
                rp.colorAttachments[0] = { .texture = res.Get(rgHDR), .clearOnLoad = true };
                rp.colorAttachmentCount = 1;
                rp.width = W; rp.height = H;
                cmd.BeginRenderPass(rp);
                cmd.EndRenderPass();
            });

        // PostFXPass: reads HDRBuffer, writes Scratch (bloom, tonemap, etc.)
        rg.AddPass("PostFXPass",
            [rgHDR, rgScratch](RGPassBuilder& b) {
                b.Read(rgHDR);
                b.Write(rgScratch);
            },
            [rgScratch, W, H](IRHICommandList& cmd, const RGResources& res) {
                RHIRenderPassDesc rp{};
                rp.colorAttachments[0] = { .texture = res.Get(rgScratch), .clearOnLoad = true };
                rp.colorAttachmentCount = 1;
                rp.width = W; rp.height = H;
                cmd.BeginRenderPass(rp);
                cmd.EndRenderPass();
            });

        // FinalPass: reads Scratch, writes swapchain (blit / UI composite)
        rg.AddPass("FinalPass",
            [rgScratch, rgSwapchain](RGPassBuilder& b) {
                b.Read(rgScratch);
                b.Write(rgSwapchain);
            },
            [rgSwapchain, W, H](IRHICommandList& cmd, const RGResources& res) {
                RHIRenderPassDesc rp{};
                rp.colorAttachments[0] = { .texture = res.Get(rgSwapchain), .clearOnLoad = false };
                rp.colorAttachmentCount = 1;
                rp.width = W; rp.height = H;
                cmd.BeginRenderPass(rp);
                cmd.EndRenderPass();
            });

        rg.Compile();
        rg.Execute(*device, *cmd);

        PrintStats(rg.GetLastFrameStats(), lastPhysical);

        device->EndFrame();
        device->Present();
        ++frameCount;
    }

    // ── Shutdown ──────────────────────────────────────────────────────────────
    device->WaitIdle();
    rg.InvalidateSlots(*device);
    device.reset();
    window.reset();
    SA_LOG_INFO("Aliasing Demo: clean shutdown ({} frames)", frameCount);
    Core::Log::Shutdown();
    return 0;
}
