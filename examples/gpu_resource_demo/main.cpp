#include "core/logs/Log.hpp"
#include "platform/window/GLFWWindow.hpp"
#include "platform/rhi/vulkan/VulkanDevice.hpp"
#include "platform/rhi/ShaderReflection.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/cook/CookedTexture.hpp"
#include "resource/vfs/VFS.hpp"
#include "GpuDemoPath.hpp"

#include <filesystem>
#include <fstream>
#include <vector>
#include <optional>

using namespace StellarAlia;
using namespace StellarAlia::RHI;
using namespace StellarAlia::Resource;
using namespace StellarAlia::Platform;
namespace fs = std::filesystem;

// ─── Utility: load a .spv file into a byte vector ────────────────────────────

static std::vector<uint8_t> LoadSpv(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        SA_LOG_ERROR("LoadSpv: cannot open '{}'", path);
        return {};
    }
    const auto size = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> data(size);
    f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
    return data;
}

// ─── Utility: find the first .satex in the cook cache ────────────────────────

static std::optional<AssetID> FindFirstTexture(const char* cacheDir) {
    fs::path dir(cacheDir);
    if (!fs::exists(dir)) return std::nullopt;
    for (auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() == ".satex") {
            const std::string stem = entry.path().stem().string();
            AssetID id = AssetID::FromString(stem);
            if (id.IsValid()) return id;
        }
    }
    return std::nullopt;
}

// ─── main ────────────────────────────────────────────────────────────────────

int main() {
    Core::Log::Initialize();
    SA_LOG_INFO("=== GPU Resource Demo (Stage 3) ===");
    SA_LOG_INFO("Cook cache : {}", GpuDemo::PROJECT_COOK_CACHE);
    SA_LOG_INFO("Shaders    : {}", GpuDemo::BUILTIN_SHADER_DIR);

    // ── Window ────────────────────────────────────────────────────────────────
    auto window = GLFWWindow::Create({
        .width    = 1280,
        .height   = 720,
        .title    = "StellarAlia - GPU Resource Demo",
        .resizable = true,
    });
    if (!window) { SA_LOG_CRITICAL("Failed to create window"); return 1; }

    // ── Device ────────────────────────────────────────────────────────────────
    auto device = VulkanDevice::Create({
        .windowHandle      = {window->GetNativeHandle()},
        .swapchainWidth    = window->GetWidth(),
        .swapchainHeight   = window->GetHeight(),
        .swapchainImageCount = 2,
        .vsync             = true,
        .enableValidation  = true,
    });
    if (!device) { SA_LOG_CRITICAL("Failed to create VulkanDevice"); return 1; }

    // ── ResourceManager ───────────────────────────────────────────────────────
    ResourceManager rm;
    rm.Init(GpuDemo::PROJECT_COOK_CACHE, device.get());

    // ── Load a texture from the cook cache ───────────────────────────────────
    auto texIdOpt = FindFirstTexture(GpuDemo::PROJECT_COOK_CACHE);
    RHITextureHandle texHandle;
    if (texIdOpt) {
        SA_LOG_INFO("Loading texture {}...", texIdOpt->ToString());
        texHandle = rm.LoadTexture(*texIdOpt);
        if (!texHandle.IsValid())
            SA_LOG_WARN("Texture upload failed - will render without texture");
    } else {
        SA_LOG_WARN("No .satex files found in cook cache - cook the project first");
    }

    // ── Load shaders ──────────────────────────────────────────────────────────
    const std::string shaderDir = GpuDemo::BUILTIN_SHADER_DIR;
    auto vertSpv = LoadSpv(shaderDir + "/fullscreen_tri.vert.spv");
    auto fragSpv = LoadSpv(shaderDir + "/fullscreen_blit.frag.spv");

    if (vertSpv.empty() || fragSpv.empty()) {
        SA_LOG_CRITICAL("Failed to load shader .spv files from {}", shaderDir);
        SA_LOG_CRITICAL("Build the project first so CMake compiles the shaders.");
        rm.Shutdown();
        device->WaitIdle();
        return 1;
    }

    // ── Build shader reflections (hardcoded for fullscreen_blit) ─────────────
    // set=0, binding=0 → combined image sampler (the source texture)
    ShaderReflection vertRefl;  // no bindings in the vertex shader

    ShaderReflection fragRefl;
    {
        ShaderBindingDesc bd;
        bd.set     = 0;
        bd.binding = 0;
        bd.type    = RHIDescriptorType::Texture2D;
        bd.stages  = RHIShaderStage::Fragment;
        bd.name    = "t_Source";
        fragRefl.bindings.push_back(bd);
    }
    ShaderReflection merged = MergeReflections(vertRefl, fragRefl);

    // ── Create shaders ────────────────────────────────────────────────────────
    RHIShaderHandle vertShader = device->CreateShader(vertSpv, vertRefl);
    RHIShaderHandle fragShader = device->CreateShader(fragSpv, fragRefl);

    if (!vertShader.IsValid() || !fragShader.IsValid()) {
        SA_LOG_CRITICAL("Shader module creation failed");
        rm.Shutdown();
        device->WaitIdle();
        return 1;
    }

    // ── Descriptor set layout + set ───────────────────────────────────────────
    RHIDescLayoutHandle descLayout = device->CreateDescriptorSetLayout(merged, 0);
    RHIDescSetHandle    descSet    = device->AllocateDescriptorSet(descLayout);

    if (texHandle.IsValid())
        device->WriteDescriptorTexture(descSet, 0, texHandle);

    // ── Pipeline ──────────────────────────────────────────────────────────────
    RHIPipelineDesc pipeDesc{};
    pipeDesc.vertShader               = vertShader;
    pipeDesc.fragShader               = fragShader;
    pipeDesc.descriptorLayouts[0]     = descLayout;
    pipeDesc.descriptorLayoutCount    = 1;
    pipeDesc.colorFormats[0]          = device->GetSwapchainFormat();
    pipeDesc.colorFormatCount  = 1;
    pipeDesc.depthFormat       = RHIFormat::Undefined;
    pipeDesc.depthTest         = false;
    pipeDesc.depthWrite        = false;
    pipeDesc.noVertexInput     = true;   // fullscreen triangle, no VB
    pipeDesc.cullMode          = RHICullMode::None;
    pipeDesc.debugName         = "fullscreen_blit";

    RHIPipelineHandle pipeline = device->CreatePipeline(pipeDesc);
    if (!pipeline.IsValid()) {
        SA_LOG_CRITICAL("Failed to create pipeline");
        rm.Shutdown();
        device->WaitIdle();
        return 1;
    }

    SA_LOG_INFO("Pipeline ready — entering render loop (close window to exit)");

    // ── Render loop ───────────────────────────────────────────────────────────
    while (!window->ShouldClose()) {
        window->PollEvents();

        static uint32_t lastW = window->GetWidth(), lastH = window->GetHeight();
        if (window->GetWidth() != lastW || window->GetHeight() != lastH) {
            lastW = window->GetWidth();
            lastH = window->GetHeight();
            device->ResizeSwapchain(lastW, lastH);
        }

        IRHICommandList* cmd = device->BeginFrame();
        if (!cmd) continue;

        const uint32_t w = device->GetSwapchainWidth();
        const uint32_t h = device->GetSwapchainHeight();

        // Blit pass: render texture to swapchain.
        // BeginFrame already cleared to the background colour; LOAD here preserves it.
        RHIRenderPassDesc passDesc{};
        passDesc.colorAttachments[0].texture      = device->GetSwapchainTexture();
        passDesc.colorAttachments[0].clearOnLoad  = false;
        passDesc.colorAttachmentCount             = 1;
        passDesc.hasDepth                         = false;
        passDesc.width                            = w;
        passDesc.height                           = h;

        cmd->BeginRenderPass(passDesc);
        cmd->SetViewport({0.f, 0.f, static_cast<float>(w), static_cast<float>(h)});
        cmd->SetScissor({0, 0, w, h});

        if (texHandle.IsValid() && pipeline.IsValid()) {
            cmd->SetPipeline(pipeline);
            cmd->SetDescriptorSet(0, descSet);
            cmd->Draw(3, 1, 0, 0);  // fullscreen triangle
        }

        cmd->EndRenderPass();

        device->EndFrame();
        device->Present();
    }

    // ── Shutdown ──────────────────────────────────────────────────────────────
    device->WaitIdle();
    device->DestroyPipeline(pipeline);
    device->DestroyShader(vertShader);
    device->DestroyShader(fragShader);
    rm.Shutdown();
    device.reset();
    window.reset();

    SA_LOG_INFO("GPU Resource Demo: clean shutdown");
    Core::Log::Shutdown();
    return 0;
}
