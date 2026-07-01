// IblDemo
//
// Full PBR IBL rendering. Loads metal_rough_spheres.sascene.
// IBL setup (offline-first, GPU bake + cache on miss), skybox, geometry,
// frame uniform management, and depth handling are all internal to SceneRenderer.
//
// Pipeline config:
//   - Shadow pass    : enabled (2048×2048)
//   - Bloom pass     : enabled (6 mip levels) — feeds into LutTonemapFeature
//   - Built-in tonemap: disabled — replaced by LutTonemapFeature below
//
// LutTonemapFeature applies ACES filmic tonemap followed by a 2D strip
// color-grading LUT (assets/textures/builtin/color_grading_lut_blue.png).
//
// Render loop — single call per frame:
//   scene.UpdateTransforms();
//   renderer.RenderFrame(scene, w, h);

#include "core/logs/Log.hpp"
#include "function/material/AttachmentKey.hpp"
#include "function/material/MaterialManager.hpp"
#include "function/material/MaterialType.hpp"
#include "function/render_graph/RenderGraph.hpp"
#include "function/renderer/RenderFeature.hpp"
#include "function/renderer/SceneRenderer.hpp"
#include "function/scene/Scene.hpp"
#include "function/scene/SceneSerializer.hpp"
#include "platform/rhi/IRHIDevice.hpp"
#include "platform/rhi/RHITypes.hpp"
#include "platform/rhi/vulkan/VulkanDevice.hpp"
#include "platform/window/GLFWWindow.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/loaders/ImageLoader.hpp"
#include "IblDemoPath.hpp"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace StellarAlia;
using namespace StellarAlia::RHI;
using namespace StellarAlia::Platform;
using namespace StellarAlia::Resource;

// ─────────────────────────────────────────────────────────────────────────────
// LutTonemapFeature
//
// Custom tonemap RenderFeature that chains ACES + a 2D strip color-grading LUT.
// Register before renderer.Init(); the LUT PNG is loaded in OnInit.
// ─────────────────────────────────────────────────────────────────────────────
class LutTonemapFeature final : public RenderFeature {
public:
    explicit LutTonemapFeature(std::string lutPath) : m_lutPath(std::move(lutPath)) {}

    void OnInit(const FeatureInitContext& ctx) override {
        ctx.matMgr->RegisterTypeFromShaders(
            {"LutTonemap", "fullscreen_tri", "postfx_lut_tonemap",
             RHI::RHICullMode::None, RHI::RHIBlendMode::Opaque, RHI::RHITopology::TriangleList, false, false, true}, ctx);
        m_type = ctx.matMgr->GetType("LutTonemap");
        if (!m_type) { SA_LOG_WARN("LutTonemapFeature: shader load failed"); return; }

        m_descSet = ctx.device->AllocateDescriptorSet(m_type->shader->GetMaterialLayout());

        // ── Load LUT PNG and upload to GPU ────────────────────────────────────
        auto img = ImageLoader::Load(m_lutPath);
        if (!img) {
            SA_LOG_ERROR("LutTonemapFeature: failed to load LUT '{}'", m_lutPath);
            return;
        }

        RHI::RHITextureDesc td{};
        td.width     = img->width;
        td.height    = img->height;
        td.format    = RHI::RHIFormat::RGBA8_UNORM;
        td.usage     = RHI::RHITextureUsage::Sampled;
        td.debugName = "ColorGradingLUT";
        m_lutTex = ctx.device->CreateTexture(td);

        const uint64_t byteSize = static_cast<uint64_t>(img->width) * img->height * 4u;
        ctx.device->UploadTextureData(m_lutTex, img->pixels.data(), byteSize);

        SA_LOG_INFO("LutTonemapFeature: loaded LUT {}x{} from '{}'",
                    img->width, img->height, m_lutPath);
    }

    void OnShutdown(RHI::IRHIDevice* device) override {
        if (m_lutTex.IsValid()) device->DestroyTexture(m_lutTex);
    }

    void AddPasses(SceneRenderer& /*renderer*/, const FrameContext& ctx,
                   const RendererHandles& handles, const entt::registry& /*reg*/,
                   uint32_t w, uint32_t h) override {
        if (!m_type || !m_descSet.IsValid() || !m_lutTex.IsValid()) return;

        if (w != m_trackedW || h != m_trackedH) {
            ctx.BindTexture(m_descSet, 0, handles.hdr);
            ctx.device->WriteDescriptorTexture(m_descSet, 1, m_lutTex);
            m_trackedW = w;
            m_trackedH = h;
        }

        AttachmentKey swapKey{};
        swapKey.colorCount      = 1;
        swapKey.colorFormats[0] = ctx.device->GetSwapchainFormat();
        swapKey.depthFormat     = RHI::RHIFormat::Undefined;

        const RHI::RHIPipelineHandle pipeline = m_type->GetOrCreatePipeline(ctx.device, swapKey);

        const RHI::RHIDescSetHandle frameSet  = ctx.frameSet;
        const RHI::RHIDescSetHandle descSet   = m_descSet;
        const RGTextureHandle rgHdr       = handles.hdr;
        const RGTextureHandle rgSwapchain = handles.swapchain;

        struct PC { float exposure; float lutStrength; float _p0; float _p1; };
        constexpr PC pc{1.0f, 1.0f, 0.f, 0.f};

        ctx.rg->AddPass("LutTonemap",
            [rgHdr, rgSwapchain](RGPassBuilder& b) {
                b.Read(rgHdr);
                b.Write(rgSwapchain);
            },
            [pipeline, frameSet, descSet, pc, rgSwapchain, w, h]
            (RHI::IRHICommandList& cmd, const RGResources& res) {
                RHI::RHIRenderPassDesc rp{};
                rp.colorAttachmentCount            = 1;
                rp.colorAttachments[0].texture     = res.Get(rgSwapchain);
                rp.colorAttachments[0].clearOnLoad = true;
                rp.width  = w;
                rp.height = h;
                cmd.BeginRenderPass(rp);
                cmd.SetViewport(RHI::RHIViewport{0.f, 0.f, float(w), float(h)});
                cmd.SetScissor(RHI::RHIScissor{0, 0, w, h});
                cmd.SetPipeline(pipeline);
                cmd.SetDescriptorSet(0, frameSet);
                cmd.SetDescriptorSet(1, descSet);
                cmd.SetPushConstants(&pc, sizeof(pc), RHI::RHIShaderStage::Fragment);
                cmd.Draw(3, 1, 0, 0);
                cmd.EndRenderPass();
            });
    }

private:
    std::string           m_lutPath;
    MaterialType*         m_type     = nullptr;
    RHI::RHIDescSetHandle m_descSet;
    RHI::RHITextureHandle m_lutTex;
    uint32_t              m_trackedW = 0;
    uint32_t              m_trackedH = 0;
};

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    Core::Log::Initialize();
    SA_LOG_INFO("IblDemo: starting");

    const fs::path assetsDir    = IblDemo::SA_ASSETS_DIR;
    const fs::path cookDir      = IblDemo::COOK_CACHE_DIR;
    const std::string shaderDir = IblDemo::BUILTIN_SHADER_DIR;

    fs::create_directories(cookDir);

    // ── Window + device ───────────────────────────────────────────────────────
    auto window = GLFWWindow::Create(WindowDesc{1280, 720, "IblDemo — MetalRoughSpheres"});

    RHIDeviceDesc devDesc{};
    devDesc.windowHandle     = NativeWindowHandle{window->GetNativeHandle()};
    devDesc.swapchainWidth   = window->GetWidth();
    devDesc.swapchainHeight  = window->GetHeight();
    devDesc.vsync            = true;
    devDesc.enableValidation = true;
    auto device = VulkanDevice::Create(devDesc);

    // ── ResourceManager + MaterialManager ────────────────────────────────────
    ResourceManager resMgr;
    resMgr.Init(cookDir.string(), device.get());

    MaterialManager matMgr;
    matMgr.Init(device.get(), &resMgr);

    // ── SceneRenderer ─────────────────────────────────────────────────────────
    SceneRenderer renderer;

    SceneRenderer::Desc rendDesc{};
    rendDesc.device       = device.get();
    rendDesc.matMgr       = &matMgr;
    rendDesc.resMgr       = &resMgr;
    rendDesc.shaderDir    = shaderDir;
    rendDesc.cookCacheDir = cookDir.string();
    rendDesc.config.shadowEnabled  = true;
    rendDesc.config.shadowMapSize  = 2048;
    rendDesc.config.bloomMipCount  = 6;

    if (!renderer.Init(rendDesc)) {
        SA_LOG_CRITICAL("IblDemo: renderer init failed");
        return 1;
    }

    // ── Load scene ────────────────────────────────────────────────────────────
    Scene scene("MetalRoughSpheres");
    const fs::path scenePath = assetsDir / "scenes" / "metal_rough_spheres.sascene";
    if (!SceneSerializer::LoadFromFile(scene, scenePath)) {
        SA_LOG_CRITICAL("IblDemo: failed to load scene '{}'", scenePath.string());
        return 1;
    }
    SA_LOG_INFO("IblDemo: scene loaded");

    // ── IBL (offline-first; GPU bake + cache on miss) ─────────────────────────
    if (!renderer.SetIBL(scene.GetWorldSettings()))
        SA_LOG_WARN("IblDemo: no IBL source — scene renders without IBL");

    // ── First transform update + draw list ────────────────────────────────────
    scene.UpdateTransforms();
    renderer.BuildDrawList(scene);

    // ─────────────────────────────────────────────────────────────────────────
    // Render loop — single call per frame
    // ─────────────────────────────────────────────────────────────────────────
    while (!window->ShouldClose()) {
        window->PollEvents();

        const int w = static_cast<int>(device->GetSwapchainWidth());
        const int h = static_cast<int>(device->GetSwapchainHeight());
        if (w <= 0 || h <= 0) continue;

        scene.UpdateTransforms();
        renderer.RenderFrame(scene, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    device->WaitIdle();
    renderer.Shutdown();
    matMgr.Shutdown();
    resMgr.Shutdown();
    device.reset();
    window.reset();

    SA_LOG_INFO("IblDemo: clean shutdown");
    Core::Log::Shutdown();
    return 0;
}
