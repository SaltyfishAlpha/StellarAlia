// ComputeDemo
//
// Exercises the compute pipeline path via ComputeProgram + ShaderProgram:
//   - ComputeProgram owns the compute shader, descriptor layouts, and pipeline.
//   - ShaderProgram owns the fullscreen-blit vert+frag pair and pipeline.
//   - RenderGraph orchestrates resource barriers between the two passes.
//
// RenderGraph pass layout per frame:
//   [ComputePass]  WriteUAV(rgOutput) → dispatches compute (animated ripple)
//   [BlitPass]     Read(rgOutput)     → fullscreen blit to swapchain

#include "core/logs/Log.hpp"
#include "function/material/AttachmentKey.hpp"
#include "function/material/ComputeProgram.hpp"
#include "function/material/ShaderProgram.hpp"
#include "function/render_graph/RenderGraph.hpp"
#include "platform/rhi/IRHICommandList.hpp"
#include "platform/rhi/IRHIDevice.hpp"
#include "platform/rhi/ShaderReflectionIO.hpp"
#include "platform/rhi/vulkan/VulkanDevice.hpp"
#include "platform/window/GLFWWindow.hpp"
#include "ComputeDemoPath.hpp"

#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;
using namespace StellarAlia;
using namespace StellarAlia::RHI;
using namespace StellarAlia::Platform;

// ─── Helpers ─────────────────────────────────────────────────────────────────

static std::vector<uint8_t> LoadSpv(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { SA_LOG_ERROR("LoadSpv: cannot open '{}'", path); return {}; }
    const auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> data(sz);
    f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(sz));
    return data;
}

static ShaderReflection LoadRefl(const std::string& path) {
    ShaderReflection refl;
    if (!ShaderReflectionIO::LoadFromFile(path, refl))
        SA_LOG_ERROR("LoadRefl: cannot open '{}'", path);
    return refl;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    Core::Log::Initialize();

    const std::string shaderDir = ComputeDemo::BUILTIN_SHADER_DIR;

    // ── Window + device ───────────────────────────────────────────────────────
    auto window = GLFWWindow::Create(WindowDesc{1280, 720, "ComputeDemo"});

    RHIDeviceDesc devDesc{};
    devDesc.windowHandle     = NativeWindowHandle{window->GetNativeHandle()};
    devDesc.swapchainWidth   = window->GetWidth();
    devDesc.swapchainHeight  = window->GetHeight();
    devDesc.vsync            = true;
    devDesc.enableValidation = true;
    auto device = VulkanDevice::Create(devDesc);

    constexpr uint32_t W = 1280, H = 720;

    // ── Storage+Sampled texture (compute writes, blit reads) ──────────────────
    RHITextureDesc outDesc{};
    outDesc.width     = W;
    outDesc.height    = H;
    outDesc.format    = RHIFormat::RGBA16F;  // mandatorily supported as storage image
    outDesc.usage     = RHITextureUsage::UnorderedAccess | RHITextureUsage::Sampled;
    outDesc.debugName = "ComputeOutput";
    RHITextureHandle outputTex = device->CreateTexture(outDesc);

    // ── ComputeProgram: shader + reflection → layout + pipeline, one call ─────
    ComputeProgram computeProg;
    if (!computeProg.Load(device.get(), {
            LoadSpv(shaderDir + "/compute_demo.comp.spv"),
            LoadRefl(shaderDir + "/compute_demo.comp.refl")})) {
        SA_LOG_CRITICAL("ComputeDemo: ComputeProgram::Load failed");
        return 1;
    }

    // Descriptor set: set=0, binding=0 = storage image written by the compute shader
    RHIDescSetHandle compDs = device->AllocateDescriptorSet(computeProg.GetLayout(0));
    device->WriteDescriptorStorageImage(compDs, 0, outputTex);

    RHIPipelineHandle compPipeline = computeProg.GetPipeline(device.get());
    if (!compPipeline.IsValid()) {
        SA_LOG_CRITICAL("ComputeDemo: compute pipeline creation failed");
        return 1;
    }

    // ── ShaderProgram: fullscreen blit ────────────────────────────────────────
    // The blit shader samples at set=0 (no per-frame globals in this pass).
    // Passing its texture layout as frameLayout places it at set=0 in the pipeline.
    ShaderProgram blitProg;
    RHIDescSetHandle  blitDs;
    RHIPipelineHandle blitPipeline;
    {
        // SPV data must be stored as named locals; std::span is a non-owning view
        // and must not be initialised from a temporary vector.
        const auto blitVertSpv  = LoadSpv(shaderDir + "/fullscreen_tri.vert.spv");
        const auto blitFragSpv  = LoadSpv(shaderDir + "/fullscreen_blit.frag.spv");
        const auto blitVertRefl = LoadRefl(shaderDir + "/fullscreen_tri.vert.refl");
        const auto blitFragRefl = LoadRefl(shaderDir + "/fullscreen_blit.frag.refl");

        const ShaderReflection blitMerged = MergeReflections(blitVertRefl, blitFragRefl);

        // Layout for the blit's source sampler (set=0, binding=0)
        RHIDescLayoutHandle blitTexLayout =
            device->CreateDescriptorSetLayout(blitMerged, 0);

        ShaderProgram::Desc blitDesc{};
        blitDesc.vertSpv     = blitVertSpv;   // span views named locals above
        blitDesc.vertRefl    = blitVertRefl;
        blitDesc.fragSpv     = blitFragSpv;
        blitDesc.fragRefl    = blitFragRefl;
        blitDesc.frameLayout = blitTexLayout; // set=0 = source texture
        if (!blitProg.Load(device.get(), blitDesc)) {
            SA_LOG_CRITICAL("ComputeDemo: ShaderProgram (blit) Load failed");
            return 1;
        }

        blitDs = device->AllocateDescriptorSet(blitTexLayout);
        device->WriteDescriptorTexture(blitDs, 0, outputTex);

        AttachmentKey blitKey{};
        blitKey.colorFormats[0] = device->GetSwapchainFormat();
        blitKey.colorCount      = 1;
        blitKey.depthFormat     = RHIFormat::Undefined;

        blitPipeline = blitProg.GetOrCreatePipeline(
            device.get(), blitKey,
            RHICullMode::None, RHIBlendMode::Opaque,
            /*depthTest=*/false, /*depthWrite=*/false, /*noVertexInput=*/true);
        if (!blitPipeline.IsValid()) {
            SA_LOG_CRITICAL("ComputeDemo: blit pipeline creation failed");
            return 1;
        }
    }

    SA_LOG_INFO("ComputeDemo: all resources created — entering render loop");

    // ── Render loop ───────────────────────────────────────────────────────────
    uint32_t frameIndex = 0;

    while (!window->ShouldClose()) {
        window->PollEvents();

        const int w = static_cast<int>(device->GetSwapchainWidth());
        const int h = static_cast<int>(device->GetSwapchainHeight());
        if (w <= 0 || h <= 0) continue;

        auto* cmd = device->BeginFrame();
        if (!cmd) continue;

        const float time        = static_cast<float>(frameIndex) / 60.f;
        const float pushData[4] = {time, 0.f, 0.f, 0.f};

        RenderGraph rg;
        rg.Reset();

        auto rgOutput    = rg.ImportTexture("ComputeOutput", outputTex,
                               RHIResourceState::Undefined,
                               RHIResourceState::ShaderRead);
        auto rgSwapchain = rg.ImportTexture("Swapchain",
                               device->GetSwapchainTexture(),
                               RHIResourceState::RenderTarget,
                               RHIResourceState::RenderTarget);

        // Compute pass: RG transitions outputTex Undefined → GENERAL before dispatch
        rg.AddPass("ComputePass",
            [rgOutput](RGPassBuilder& b) { b.WriteUAV(rgOutput); },
            [compPipeline, compDs, &pushData, W, H]
            (IRHICommandList& cmd, const RGResources&)
        {
            cmd.SetComputePipeline(compPipeline);
            cmd.SetDescriptorSet(0, compDs);
            cmd.SetPushConstants(pushData, sizeof(pushData), RHIShaderStage::Compute);
            cmd.Dispatch((W + 7) / 8, (H + 7) / 8, 1);
        });

        // Blit pass: RG transitions outputTex GENERAL → SHADER_READ_ONLY before draw
        rg.AddPass("BlitPass",
            [rgOutput, rgSwapchain](RGPassBuilder& b) {
                b.Read(rgOutput);
                b.Write(rgSwapchain);
            },
            [blitPipeline, blitDs, w, h, rgSwapchain]
            (IRHICommandList& cmd, const RGResources& res)
        {
            RHIRenderPassDesc rpDesc{};
            rpDesc.colorAttachmentCount            = 1;
            rpDesc.colorAttachments[0].texture     = res.Get(rgSwapchain);
            rpDesc.colorAttachments[0].clearOnLoad = true;
            rpDesc.hasDepth                        = false;
            rpDesc.width  = static_cast<uint32_t>(w);
            rpDesc.height = static_cast<uint32_t>(h);

            cmd.BeginRenderPass(rpDesc);
            cmd.SetViewport(RHIViewport{0.f, 0.f, float(w), float(h)});
            cmd.SetScissor(RHIScissor{0, 0, uint32_t(w), uint32_t(h)});
            cmd.SetPipeline(blitPipeline);
            cmd.SetDescriptorSet(0, blitDs);
            cmd.Draw(3, 1, 0, 0);
            cmd.EndRenderPass();
        });

        rg.Compile();
        rg.Execute(*device, *cmd);

        device->EndFrame();
        device->Present();
        ++frameIndex;
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    device->WaitIdle();
    blitProg.Unload(device.get());
    computeProg.Unload(device.get());
    device->DestroyTexture(outputTex);
    device.reset();
    window.reset();
    SA_LOG_INFO("ComputeDemo: clean shutdown");
    Core::Log::Shutdown();
    return 0;
}
