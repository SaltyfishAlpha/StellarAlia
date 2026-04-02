// SkyboxDemo — minimal equirectangular HDR skybox, rotating camera.
// No geometry, no materials.  Isolates the skybox rendering path.

#include "core/logs/Log.hpp"
#include "platform/window/GLFWWindow.hpp"
#include "platform/rhi/vulkan/VulkanDevice.hpp"
#include "platform/rhi/ShaderReflection.hpp"
#include "function/FrameUniforms.hpp"
#include "function/FrameUniformsBuffer.hpp"
#include "resource/loaders/ImageLoader.hpp"
#include "SkyboxDemoPath.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;
using namespace StellarAlia;
using namespace StellarAlia::RHI;
using namespace StellarAlia::Platform;
using namespace StellarAlia::Resource;

// ── helpers ──────────────────────────────────────────────────────────────────

static std::vector<uint8_t> LoadSpv(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { SA_LOG_ERROR("LoadSpv: cannot open '{}'", path); return {}; }
    const auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> data(sz);
    f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(sz));
    return data;
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    Core::Log::Initialize();
    SA_LOG_INFO("=== SkyboxDemo ===");

    const fs::path assetsDir  = SkyboxDemo::SA_ASSETS_DIR;
    const std::string shaderDir = SkyboxDemo::BUILTIN_SHADER_DIR;
    const fs::path hdriPath   = assetsDir / "hdri/grasslands_sunset_4k.hdr";

    // ── Window ────────────────────────────────────────────────────────────────
    auto window = GLFWWindow::Create({
        .width=1280, .height=720,
        .title="StellarAlia - Skybox Demo",
        .resizable=true,
    });
    if (!window) { SA_LOG_CRITICAL("Window creation failed"); return 1; }

    // ── Device ────────────────────────────────────────────────────────────────
    auto device = VulkanDevice::Create({
        .windowHandle        = {window->GetNativeHandle()},
        .swapchainWidth      = window->GetWidth(),
        .swapchainHeight     = window->GetHeight(),
        .swapchainImageCount = 2,
        .vsync               = true,
        .enableValidation    = true,
    });
    if (!device) { SA_LOG_CRITICAL("Device creation failed"); return 1; }

    // ── FrameUniformsBuffer (set=0) ───────────────────────────────────────────
    FrameUniformsBuffer frameUniforms;
    frameUniforms.Init(device.get());

    // ── Load HDR env map ──────────────────────────────────────────────────────
    RHITextureHandle envMapTex;
    {
        auto imgOpt = ImageLoader::LoadHDR(hdriPath.string());
        if (!imgOpt || !imgOpt->IsValid() || imgOpt->pixelsHDR.empty()) {
            SA_LOG_CRITICAL("Failed to load HDR: {}", hdriPath.string());
            frameUniforms.Shutdown();
            device->WaitIdle();
            return 1;
        }
        const ImageData& img = *imgOpt;
        const uint32_t ch  = img.channels;
        const uint32_t npx = img.width * img.height;

        std::vector<float> rgba(npx * 4);
        for (uint32_t p = 0; p < npx; ++p) {
            rgba[p*4+0] = (ch > 0) ? img.pixelsHDR[p*ch+0] : 0.f;
            rgba[p*4+1] = (ch > 1) ? img.pixelsHDR[p*ch+1] : 0.f;
            rgba[p*4+2] = (ch > 2) ? img.pixelsHDR[p*ch+2] : 0.f;
            rgba[p*4+3] = 1.f;
        }
        RHITextureDesc td{};
        td.width     = img.width;
        td.height    = img.height;
        td.format    = RHIFormat::RGBA32F;
        td.usage     = RHITextureUsage::Sampled;
        td.debugName = "EnvMap";
        envMapTex = device->CreateTexture(td);
        device->UploadTextureData(envMapTex, rgba.data(), rgba.size() * sizeof(float));
        SA_LOG_INFO("Env map loaded: {}x{}", img.width, img.height);
    }

    // 1×1 white placeholder for t_BrdfLut (slot 2 in set=0; unused here)
    RHITextureHandle whiteTex;
    {
        RHITextureDesc td{};
        td.width = 1; td.height = 1;
        td.format = RHIFormat::RGBA8_UNORM;
        td.usage  = RHITextureUsage::Sampled;
        td.debugName = "White1x1";
        whiteTex = device->CreateTexture(td);
        const uint32_t w = 0xFFFFFFFFu;
        device->UploadTextureData(whiteTex, &w, 4);
    }
    frameUniforms.SetIBLTextures(whiteTex, whiteTex, envMapTex);

    // ── Skybox shaders ────────────────────────────────────────────────────────
    auto vertSpv = LoadSpv(shaderDir + "/skybox.vert.spv");
    auto fragSpv = LoadSpv(shaderDir + "/skybox.frag.spv");
    if (vertSpv.empty() || fragSpv.empty()) {
        SA_LOG_CRITICAL("Skybox shaders not found in '{}'", shaderDir);
        device->DestroyTexture(envMapTex);
        device->DestroyTexture(whiteTex);
        frameUniforms.Shutdown();
        device->WaitIdle();
        return 1;
    }
    SA_LOG_INFO("Skybox SPVs loaded: vert={}B  frag={}B", vertSpv.size(), fragSpv.size());

    ShaderReflection emptyRefl;
    RHIShaderHandle  skyVert = device->CreateShader(vertSpv, emptyRefl);
    RHIShaderHandle  skyFrag = device->CreateShader(fragSpv, emptyRefl);
    if (!skyVert.IsValid() || !skyFrag.IsValid()) {
        SA_LOG_CRITICAL("Shader module creation failed");
        device->DestroyTexture(envMapTex);
        device->DestroyTexture(whiteTex);
        frameUniforms.Shutdown();
        device->WaitIdle();
        return 1;
    }
    SA_LOG_INFO("Shader modules created");

    // ── Skybox pipeline (no depth attachment) ─────────────────────────────────
    RHIPipelineDesc pd{};
    pd.vertShader            = skyVert;
    pd.fragShader            = skyFrag;
    pd.descriptorLayouts[0]  = frameUniforms.GetLayout();
    pd.descriptorLayoutCount = 1;
    pd.colorFormatCount      = 1;
    pd.colorFormats[0]       = device->GetSwapchainFormat();
    pd.depthFormat           = RHIFormat::Undefined;   // no depth attachment
    pd.depthTest             = false;
    pd.depthWrite            = false;
    pd.noVertexInput         = true;
    pd.cullMode              = RHICullMode::None;
    pd.debugName             = "SkyboxPipeline";
    RHIPipelineHandle skyboxPipeline = device->CreatePipeline(pd);

    if (!skyboxPipeline.IsValid()) {
        SA_LOG_CRITICAL("Skybox pipeline creation FAILED");
        device->DestroyShader(skyVert);
        device->DestroyShader(skyFrag);
        device->DestroyTexture(envMapTex);
        device->DestroyTexture(whiteTex);
        frameUniforms.Shutdown();
        device->WaitIdle();
        return 1;
    }
    SA_LOG_INFO("Skybox pipeline created successfully");

    // ── Render loop ───────────────────────────────────────────────────────────
    SA_LOG_INFO("Entering render loop — camera rotates continuously");

    float time   = 0.f;
    uint32_t lastW = window->GetWidth(), lastH = window->GetHeight();

    while (!window->ShouldClose()) {
        window->PollEvents();

        const uint32_t W = window->GetWidth(), H = window->GetHeight();
        if (W != lastW || H != lastH) {
            lastW = W; lastH = H;
            device->WaitIdle();
            device->ResizeSwapchain(lastW, lastH);
        }

        IRHICommandList* cmd = device->BeginFrame();
        if (!cmd) continue;

        const uint32_t w  = device->GetSwapchainWidth();
        const uint32_t h  = device->GetSwapchainHeight();
        const uint32_t fi = device->GetCurrentFrameIndex();

        // ── Upload frame uniforms ─────────────────────────────────────────────
        time += 0.016f;

        // Rotate camera yaw continuously; slight pitch bob for variety
        const float yaw   = time * 0.3f;                    // full rotation ~21 s
        const float pitch = glm::sin(time * 0.15f) * 0.35f; // ±20° vertical bob

        const glm::vec3 camPos(0.f, 0.f, 0.f);
        const glm::vec3 front(
            glm::cos(pitch) * glm::cos(yaw),
            glm::sin(pitch),
            glm::cos(pitch) * glm::sin(yaw));

        FrameUniforms fu{};
        fu.view        = glm::lookAt(camPos, camPos + front, glm::vec3(0.f, 1.f, 0.f));
        fu.proj        = glm::perspective(glm::radians(90.f),
                                           static_cast<float>(w) / static_cast<float>(h),
                                           0.1f, 1000.f);
        fu.proj[1][1] *= -1.f;
        fu.viewProj    = fu.proj * fu.view;
        fu.invViewProj = glm::inverse(fu.viewProj);
        fu.cameraPos   = camPos;
        fu.time        = time;
        fu.resolution  = glm::vec2(static_cast<float>(w), static_cast<float>(h));
        fu.deltaTime   = 0.016f;

        LightUniforms lu{};  // unused but required by FrameUniformsBuffer
        lu.direction = glm::normalize(glm::vec3(1.f, -1.f, 0.5f));
        lu.intensity = 1.f;
        lu.color     = glm::vec3(1.f);

        frameUniforms.Upload(fi, fu, lu);

        // ── Skybox render pass (color only, no depth) ─────────────────────────
        RHIRenderPassDesc passDesc{};
        passDesc.colorAttachments[0].texture     = device->GetSwapchainTexture();
        passDesc.colorAttachments[0].clearOnLoad = false;  // keep BeginFrame clear
        passDesc.colorAttachmentCount = 1;
        passDesc.hasDepth = false;
        passDesc.width    = w;
        passDesc.height   = h;

        cmd->BeginRenderPass(passDesc);
        cmd->SetViewport({0.f, 0.f, static_cast<float>(w), static_cast<float>(h)});
        cmd->SetScissor({0, 0, w, h});
        cmd->SetPipeline(skyboxPipeline);
        cmd->SetDescriptorSet(0, frameUniforms.GetDescriptorSet(
            fi % FrameUniformsBuffer::MAX_FRAMES));
        cmd->Draw(3, 1, 0, 0);
        cmd->EndRenderPass();

        device->EndFrame();
        device->Present();
    }

    // ── Shutdown ──────────────────────────────────────────────────────────────
    device->WaitIdle();
    device->DestroyPipeline(skyboxPipeline);
    device->DestroyShader(skyVert);
    device->DestroyShader(skyFrag);
    device->DestroyTexture(envMapTex);
    device->DestroyTexture(whiteTex);
    frameUniforms.Shutdown();
    device.reset();
    window.reset();

    SA_LOG_INFO("SkyboxDemo: clean shutdown");
    Core::Log::Shutdown();
    return 0;
}
