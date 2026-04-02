// IblDemo
//
// Full PBR IBL rendering with GPU-computed IBL maps.
//
// On startup:
//   1. Loads metal_rough_spheres.sascene — provides WorldSettings (IBL asset UUIDs).
//   2. IBL setup (offline-first):
//        a. If sh9 + prefilteredEnv + brdfLut + skyboxCubemap are all cached → load directly.
//        b. Otherwise run GpuIblBake (CPU SH projection + GPU compute) and cache all results.
//   3. Renders with full PBR IBL (Cook-Torrance + SH diffuse + specular split-sum)
//      and a cubemap skybox.

#include "core/logs/Log.hpp"
#include "function/FrameUniforms.hpp"
#include "function/FrameUniformsBuffer.hpp"
#include "function/ibl/GpuIblBake.hpp"
#include "function/material/AttachmentKey.hpp"
#include "function/material/MaterialInstance.hpp"
#include "function/material/MaterialManager.hpp"
#include "function/material/MaterialType.hpp"
#include "function/material/ShaderProgram.hpp"
#include "function/render_graph/RenderGraph.hpp"
#include "function/scene/Components.hpp"
#include "function/scene/Scene.hpp"
#include "function/scene/SceneSerializer.hpp"
#include "platform/rhi/ShaderReflection.hpp"
#include "platform/rhi/ShaderReflectionIO.hpp"
#include "platform/rhi/vulkan/VulkanDevice.hpp"
#include "platform/window/GLFWWindow.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/cook/CookedSH9.hpp"
#include "resource/cook/CookedTexture.hpp"
#include "IblDemoPath.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace StellarAlia;
using namespace StellarAlia::RHI;
using namespace StellarAlia::Platform;
using namespace StellarAlia::Resource;

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

static std::unique_ptr<MaterialType> BuildDefaultPbrType(
    IRHIDevice*         device,
    RHIDescLayoutHandle frameLayout,
    const std::string&  shaderDir)
{
    const auto vertSpv = LoadSpv(shaderDir + "/pbr.vert.spv");
    const auto fragSpv = LoadSpv(shaderDir + "/pbr.frag.spv");
    if (vertSpv.empty() || fragSpv.empty()) return nullptr;

    ShaderReflection vertRefl, fragRefl;
    if (!ShaderReflectionIO::LoadFromFile(shaderDir + "/pbr.vert.refl", vertRefl) ||
        !ShaderReflectionIO::LoadFromFile(shaderDir + "/pbr.frag.refl", fragRefl)) {
        SA_LOG_ERROR("BuildDefaultPbrType: failed to load .refl files");
        return nullptr;
    }

    const ShaderReflection merged = MergeReflections(vertRefl, fragRefl);

    auto type  = std::make_unique<MaterialType>();
    type->name = "PBR";

    if (auto ubo = merged.FindBinding(1, 0))
        type->uboSize = ubo->blockSize;
    if (auto ubo = merged.FindBinding(1, 0))
        for (const auto& m : ubo->members)
            type->params.push_back({m.name, m.offset, m.size});

    {
        std::vector<const ShaderBindingDesc*> texBindings;
        for (const auto& b : merged.bindings) {
            if (b.set != 1) continue;
            if (b.type == RHIDescriptorType::Texture2D  ||
                b.type == RHIDescriptorType::TextureCube ||
                b.type == RHIDescriptorType::Sampler)
                texBindings.push_back(&b);
        }
        std::sort(texBindings.begin(), texBindings.end(),
            [](const ShaderBindingDesc* a, const ShaderBindingDesc* b){
                return a->binding < b->binding; });
        uint32_t slotIdx = 0;
        for (const auto* b : texBindings)
            type->textures.push_back({b->name, b->binding, slotIdx++});
    }

    type->defaultCullMode   = RHICullMode::Back;
    type->defaultDepthTest  = true;
    type->defaultDepthWrite = true;

    ShaderProgram::Desc pd;
    pd.vertSpv     = vertSpv;
    pd.vertRefl    = vertRefl;
    pd.fragSpv     = fragSpv;
    pd.fragRefl    = fragRefl;
    pd.frameLayout = frameLayout;
    if (!type->shader.Load(device, pd)) return nullptr;

    return type;
}

// ─── Per-frame draw item ──────────────────────────────────────────────────────

struct DrawItem {
    RHIBufferHandle   vertexBuffer;
    RHIBufferHandle   indexBuffer;
    uint32_t          firstIndex;
    uint32_t          indexCount;
    int32_t           vertexOffset;
    MaterialInstance* material;
    RHIPipelineHandle pipeline;
    uint32_t          pushConstantSize;
    glm::mat4         worldMatrix;
};

// ─── main ────────────────────────────────────────────────────────────────────

int main() {
    Core::Log::Initialize();

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

    // ── Frame uniforms ────────────────────────────────────────────────────────
    FrameUniformsBuffer frameUniforms;
    frameUniforms.Init(device.get());

    // ── Depth texture ─────────────────────────────────────────────────────────
    RHITextureDesc depthDesc{};
    depthDesc.width     = window->GetWidth();
    depthDesc.height    = window->GetHeight();
    depthDesc.format    = RHIFormat::D32F;
    depthDesc.usage     = RHITextureUsage::DepthStencil;
    depthDesc.debugName = "Depth";
    RHITextureHandle depthTex = device->CreateTexture(depthDesc);

    // 1×1 white placeholder texture
    RHITextureDesc whiteDesc{};
    whiteDesc.width     = 1;
    whiteDesc.height    = 1;
    whiteDesc.format    = RHIFormat::RGBA8_UNORM;
    whiteDesc.usage     = RHITextureUsage::Sampled;
    whiteDesc.debugName = "White1x1";
    RHITextureHandle whiteTex = device->CreateTexture(whiteDesc);
    const uint32_t whitePixel = 0xFFFFFFFFu;
    device->UploadTextureData(whiteTex, &whitePixel, sizeof(whitePixel));

    // ── ResourceManager ───────────────────────────────────────────────────────
    ResourceManager resMgr;
    resMgr.Init(cookDir.string(), device.get());

    // ── Material system ───────────────────────────────────────────────────────
    MaterialManager matMgr;
    matMgr.Init(device.get(), whiteTex);

    auto pbrType = BuildDefaultPbrType(device.get(), frameUniforms.GetLayout(), shaderDir);
    if (!pbrType) {
        SA_LOG_CRITICAL("IblDemo: failed to build PBR material type");
        return 1;
    }
    matMgr.RegisterType(std::move(pbrType));

    // ── Load scene (provides WorldSettings — skybox HDR UUID + offline IBL) ───
    const fs::path scenePath = assetsDir / "scenes" / "metal_rough_spheres.sascene";
    Scene scene("MetalRoughSpheres");
    if (!SceneSerializer::LoadFromFile(scene, scenePath)) {
        SA_LOG_CRITICAL("IblDemo: failed to load scene '{}'", scenePath.string());
        return 1;
    }
    SA_LOG_INFO("IblDemo: scene loaded");

    // ── IBL setup — offline-first, GPU bake as fallback ──────────────────────
    //
    // If WorldSettings has valid sh9/prefilteredEnv/brdfLut UUIDs (produced by
    // the IblBake tool), load them directly from the cook cache.
    // Otherwise run GpuIblBake to compute them at runtime (~100 ms).

    const WorldSettings& ws = scene.GetWorldSettings();

    glm::vec4        shCoeffs[9]  = {};
    RHITextureHandle brdfLutTex;
    RHITextureHandle prefilteredEnvTex;
    RHITextureHandle skyboxCubemapTex;
    bool             gpuBakeOwnsTextures = false;

    const bool canLoadOffline = ws.sh9.IsValid() &&
                                ws.prefilteredEnv.IsValid() &&
                                ws.brdfLut.IsValid() &&
                                ws.skyboxCubemap.IsValid();
    bool offlineOk = false;

    if (canLoadOffline) {
        auto sh9Opt = resMgr.LoadSH9Coeffs(ws.sh9);
        auto blt    = resMgr.LoadTexture(ws.brdfLut);
        auto pet    = resMgr.LoadTexture(ws.prefilteredEnv);
        auto sky    = resMgr.LoadTexture(ws.skyboxCubemap);

        if (sh9Opt && blt.IsValid() && pet.IsValid() && sky.IsValid()) {
            for (int i = 0; i < 9; ++i) shCoeffs[i] = (*sh9Opt)[i];
            brdfLutTex        = blt;
            prefilteredEnvTex = pet;
            skyboxCubemapTex  = sky;
            offlineOk         = true;
            SA_LOG_INFO("IblDemo: IBL loaded from cook cache (offline)");
        } else {
            SA_LOG_WARN("IblDemo: offline IBL load incomplete, falling back to GPU bake");
        }
    }

    if (!offlineOk) {
        auto hdrOpt = resMgr.LoadHDRImageData(ws.skyboxHdr);
        if (!hdrOpt) {
            SA_LOG_CRITICAL("IblDemo: failed to load skybox HDR (UUID={})",
                            ws.skyboxHdr.ToString());
            return 1;
        }
        SA_LOG_INFO("IblDemo: HDR loaded ({}×{})", hdrOpt->width, hdrOpt->height);

        GpuIblBake gpuBake;
        if (!gpuBake.Init(device.get(), shaderDir)) {
            SA_LOG_CRITICAL("IblDemo: GpuIblBake::Init failed — IBL shaders missing");
            return 1;
        }
        const GpuIblBake::Result iblResult = gpuBake.Bake(device.get(), *hdrOpt);
        gpuBake.Shutdown(device.get());

        if (!iblResult.IsValid()) {
            SA_LOG_CRITICAL("IblDemo: GpuIblBake::Bake failed");
            return 1;
        }

        std::copy(std::begin(iblResult.shCoeffs), std::end(iblResult.shCoeffs),
                  std::begin(shCoeffs));
        brdfLutTex            = iblResult.brdfLut;
        prefilteredEnvTex     = iblResult.prefilteredEnv;
        skyboxCubemapTex      = iblResult.skyboxCubemap;
        gpuBakeOwnsTextures   = true;
        SA_LOG_INFO("IblDemo: GPU IBL bake complete");

        // Cache SH9 coefficients.
        if (ws.sh9.IsValid()) {
            Resource::CookedSH9 sh9Cache;
            sh9Cache.id = ws.sh9;
            for (int i = 0; i < 9; ++i) sh9Cache.coeffs[i] = shCoeffs[i];
            const fs::path sh9Path = cookDir / (ws.sh9.ToString() + ".sash9");
            if (Resource::SaveCookedSH9(sh9Cache, sh9Path.string()))
                SA_LOG_INFO("IblDemo: SH9 cached → {}", sh9Path.filename().string());
        }

        // Cache GPU textures — readback from GPU → save .satex for next launch.
        // isCubemap controls whether the cubemap flag is set in the output file.
        auto saveGpuTex = [&](RHITextureHandle tex,
                               const AssetID&   id,
                               bool             isHDR,
                               bool             isCubemap) {
            if (!id.IsValid()) return;
            const RHITextureDesc* desc = device->GetTextureDesc(tex);
            if (!desc) return;

            // For cubemaps each mip block covers all 6 faces:
            //   size per mip = faceW * faceH * 4 * sizeof(float) * 6
            const uint32_t bytesPerPixel = 4 * sizeof(float); // RGBA32F
            const uint32_t mipCount      = desc->mipLevels;
            const uint32_t numLayers     = isCubemap ? 6u : 1u;

            std::vector<std::vector<uint8_t>>    mipData(mipCount);
            std::vector<IRHIDevice::MipReadback> readbacks(mipCount);
            for (uint32_t m = 0; m < mipCount; ++m) {
                const uint32_t mW = std::max(1u, desc->width  >> m);
                const uint32_t mH = std::max(1u, desc->height >> m);
                const uint64_t sz = static_cast<uint64_t>(mW) * mH * bytesPerPixel * numLayers;
                mipData[m].resize(sz);
                readbacks[m] = { mipData[m].data(), sz };
            }
            device->ReadbackTextureMips(tex, readbacks);

            Resource::CookedTexture cooked;
            cooked.id        = id;
            cooked.width     = desc->width;
            cooked.height    = desc->height;
            cooked.mipLevels = mipCount;
            cooked.format    = Resource::CookedTextureFormat::RGBA32F;
            cooked.srgb      = false;
            cooked.isHDR     = isHDR;
            cooked.cubemap   = isCubemap;

            uint64_t offset = 0;
            for (uint32_t m = 0; m < mipCount; ++m) {
                cooked.mips.push_back({ offset, static_cast<uint64_t>(mipData[m].size()) });
                cooked.data.insert(cooked.data.end(),
                                   mipData[m].begin(), mipData[m].end());
                offset += mipData[m].size();
            }

            const fs::path outPath = cookDir / (id.ToString() + ".satex");
            if (Resource::SaveCookedTexture(cooked, outPath.string()))
                SA_LOG_INFO("IblDemo: {} cached → {}", id.ToString().substr(0,8),
                            outPath.filename().string());
        };

        saveGpuTex(iblResult.brdfLut,        ws.brdfLut,        false, false);
        saveGpuTex(iblResult.prefilteredEnv,  ws.prefilteredEnv, true,  true);
        saveGpuTex(iblResult.skyboxCubemap,   ws.skyboxCubemap,  true,  true);
    }

    frameUniforms.SetIBLTextures(brdfLutTex, prefilteredEnvTex, skyboxCubemapTex);
    SA_LOG_INFO("IblDemo: IBL textures bound");

    scene.UpdateTransforms();

    // ── Resolve meshes + build draw items ─────────────────────────────────────
    std::vector<DrawItem> drawItems;

    scene.View<StaticMeshComponent, WorldTransformComponent>().each(
        [&](entt::entity e,
            const StaticMeshComponent&     meshComp,
            const WorldTransformComponent& world)
    {
        if (!meshComp.meshAsset.IsValid()) return;

        const GPUMesh* gpuMesh = resMgr.LoadMesh(meshComp.meshAsset);
        if (!gpuMesh) {
            SA_LOG_WARN("IblDemo: mesh {} not found in cook cache",
                        meshComp.meshAsset.ToString());
            return;
        }

        for (size_t si = 0; si < gpuMesh->subMeshes.size(); ++si) {
            const auto& sub = gpuMesh->subMeshes[si];

            MaterialInstance* inst =
                matMgr.LoadMaterial(sub.defaultMaterialID, cookDir, resMgr);
            if (!inst) continue;

            // Apply any per-entity material overrides on top of the .samat defaults.
            if (const auto* ov = scene.Registry().try_get<MaterialOverrideComponent>(e)) {
                for (const auto& p : ov->params)
                    inst->SetRawParam(p.name, p.value.data(),
                                      static_cast<uint32_t>(p.value.size()));
            }

            DrawItem item{};
            item.vertexBuffer     = gpuMesh->vertexBuffer;
            item.indexBuffer      = gpuMesh->indexBuffer;
            item.firstIndex       = sub.firstIndex;
            item.indexCount       = sub.indexCount;
            item.vertexOffset     = sub.vertexOffset;
            item.material         = inst;
            item.worldMatrix      = world.matrix * sub.localTransform;
            item.pushConstantSize = inst->GetType()->shader
                                        .GetMergedReflection().pushConstantSize;
            drawItems.push_back(item);
        }
    });

    SA_LOG_INFO("IblDemo: {} draw item(s) prepared", drawItems.size());

    // ── Attachment key descriptors ────────────────────────────────────────────
    AttachmentKey skyboxKey{};
    skyboxKey.colorFormats[0] = device->GetSwapchainFormat();
    skyboxKey.colorCount      = 1;
    skyboxKey.depthFormat     = RHIFormat::Undefined;

    AttachmentKey geoKey{};
    geoKey.colorCount      = 1;
    geoKey.colorFormats[0] = device->GetSwapchainFormat();
    geoKey.depthFormat     = RHIFormat::D32F;

    for (auto& item : drawItems)
        item.pipeline = item.material->GetPipeline(device.get(), geoKey);

    // ── Skybox shader ─────────────────────────────────────────────────────────
    ShaderProgram skyboxProgram;
    {
        auto skyboxVertSpv = LoadSpv(shaderDir + "/skybox.vert.spv");
        auto skyboxFragSpv = LoadSpv(shaderDir + "/skybox.frag.spv");
        ShaderReflection skyboxVertRefl, skyboxFragRefl;
        const bool ok =
            !skyboxVertSpv.empty() && !skyboxFragSpv.empty() &&
            ShaderReflectionIO::LoadFromFile(shaderDir + "/skybox.vert.refl", skyboxVertRefl) &&
            ShaderReflectionIO::LoadFromFile(shaderDir + "/skybox.frag.refl", skyboxFragRefl);
        if (ok) {
            ShaderProgram::Desc pd;
            pd.vertSpv     = skyboxVertSpv;
            pd.vertRefl    = skyboxVertRefl;
            pd.fragSpv     = skyboxFragSpv;
            pd.fragRefl    = skyboxFragRefl;
            pd.frameLayout = frameUniforms.GetLayout();
            if (skyboxProgram.Load(device.get(), pd))
                SA_LOG_INFO("IblDemo: skybox shader loaded");
            else
                SA_LOG_ERROR("IblDemo: skybox shader load failed");
        }
    }

    // ── Render loop ───────────────────────────────────────────────────────────
    uint32_t frameIndex = 0;

    while (!window->ShouldClose()) {
        window->PollEvents();

        int w = static_cast<int>(device->GetSwapchainWidth());
        int h = static_cast<int>(device->GetSwapchainHeight());
        if (w <= 0 || h <= 0) continue;

        // Recreate depth texture if the swapchain was resized.
        if (static_cast<uint32_t>(w) != depthDesc.width ||
            static_cast<uint32_t>(h) != depthDesc.height) {
            device->WaitIdle();
            device->DestroyTexture(depthTex);
            depthDesc.width  = static_cast<uint32_t>(w);
            depthDesc.height = static_cast<uint32_t>(h);
            depthTex = device->CreateTexture(depthDesc);
        }

        scene.UpdateTransforms();

        // Camera
        FrameUniforms fu{};
        fu.resolution = {static_cast<float>(w), static_cast<float>(h)};
        fu.time       = static_cast<float>(frameIndex) / 60.f;
        std::copy(std::begin(shCoeffs), std::end(shCoeffs), std::begin(fu.irrSH));
        scene.View<CameraComponent, ActiveCameraTag, WorldTransformComponent>().each(
            [&](auto, const CameraComponent& cam, const WorldTransformComponent& wt)
        {
            const float aspect = static_cast<float>(w) / static_cast<float>(h);
            fu.view         = glm::inverse(wt.matrix);
            fu.proj         = glm::perspective(cam.fovY, aspect, cam.nearPlane, cam.farPlane);
            fu.proj[1][1]  *= -1.f;
            fu.viewProj     = fu.proj * fu.view;
            fu.invViewProj  = glm::inverse(fu.viewProj);
            fu.cameraPos    = glm::vec3(wt.matrix[3]);
        });

        // Lights — collect directional, point, and spot from the scene
        LightUniforms lu{};
        int lightIdx = 0;
        scene.View<DirectionalLightComponent, TransformComponent>().each(
            [&](auto, const DirectionalLightComponent& dl, const TransformComponent& t) {
                if (lightIdx >= LightUniforms::MAX_LIGHTS) return;
                auto& e      = lu.lights[lightIdx++];
                e.direction  = glm::normalize(t.rotation * glm::vec3(0.f, 0.f, -1.f));
                e.color      = dl.color;
                e.intensity  = dl.intensity;
                e.type       = 0;
            });
        scene.View<PointLightComponent, WorldTransformComponent>().each(
            [&](auto, const PointLightComponent& pl, const WorldTransformComponent& wt) {
                if (lightIdx >= LightUniforms::MAX_LIGHTS) return;
                auto& e     = lu.lights[lightIdx++];
                e.position  = glm::vec3(wt.matrix[3]);
                e.color     = pl.color;
                e.intensity = pl.intensity;
                e.range     = pl.range;
                e.type      = 1;
            });
        scene.View<SpotLightComponent, TransformComponent, WorldTransformComponent>().each(
            [&](auto, const SpotLightComponent& sl,
                const TransformComponent& t, const WorldTransformComponent& wt) {
                if (lightIdx >= LightUniforms::MAX_LIGHTS) return;
                auto& e       = lu.lights[lightIdx++];
                e.position    = glm::vec3(wt.matrix[3]);
                e.direction   = glm::normalize(t.rotation * glm::vec3(0.f, 0.f, -1.f));
                e.color       = sl.color;
                e.intensity   = sl.intensity;
                e.range       = sl.range;
                e.innerAngle  = sl.innerAngle;
                e.outerAngle  = sl.outerAngle;
                e.type        = 2;
            });
        lu.lightCount = lightIdx;

        auto* cmd = device->BeginFrame();
        if (!cmd) continue;

        const uint32_t fi = device->GetCurrentFrameIndex();
        frameUniforms.Upload(fi, fu, lu);

        RenderGraph rg;
        rg.Reset();

        auto rgSwapchain = rg.ImportTexture("Swapchain",
            device->GetSwapchainTexture(),
            RHIResourceState::RenderTarget,
            RHIResourceState::RenderTarget);
        auto rgDepth = rg.ImportTexture("Depth",
            depthTex,
            RHIResourceState::Undefined,
            RHIResourceState::Undefined);

        const auto frameDescSet = frameUniforms.GetDescriptorSet(fi);
        const int localW = w, localH = h;

        // Skybox pass
        if (skyboxProgram.IsLoaded()) {
            RHIPipelineHandle skyboxPipeline = skyboxProgram.GetOrCreatePipeline(
                device.get(), skyboxKey,
                RHICullMode::None, RHIBlendMode::Opaque,
                /*depthTest*/false, /*depthWrite*/false, /*noVertexInput*/true);

            rg.AddPass("Skybox",
                [rgSwapchain](RGPassBuilder& b) { b.Write(rgSwapchain); },
                [skyboxPipeline, frameDescSet, localW, localH, rgSwapchain]
                (IRHICommandList& cmd, const RGResources& res)
            {
                RHIRenderPassDesc rpDesc{};
                rpDesc.colorAttachmentCount            = 1;
                rpDesc.colorAttachments[0].texture     = res.Get(rgSwapchain);
                rpDesc.colorAttachments[0].clearOnLoad = false;
                rpDesc.hasDepth                        = false;
                rpDesc.width  = static_cast<uint32_t>(localW);
                rpDesc.height = static_cast<uint32_t>(localH);
                cmd.BeginRenderPass(rpDesc);
                cmd.SetViewport(RHIViewport{0.f, 0.f, float(localW), float(localH)});
                cmd.SetScissor(RHIScissor{0, 0, uint32_t(localW), uint32_t(localH)});
                cmd.SetPipeline(skyboxPipeline);
                cmd.SetDescriptorSet(0, frameDescSet);
                cmd.Draw(3, 1, 0, 0);
                cmd.EndRenderPass();
            });
        }

        // Geometry pass
        rg.AddPass("Geometry",
            [rgSwapchain, rgDepth](RGPassBuilder& b) {
                b.Write(rgSwapchain);
                b.WriteDepth(rgDepth);
            },
            [&drawItems, frameDescSet, localW, localH, rgSwapchain, rgDepth]
            (IRHICommandList& cmd, const RGResources& res)
        {
            RHIRenderPassDesc rpDesc{};
            rpDesc.colorAttachmentCount              = 1;
            rpDesc.colorAttachments[0].texture       = res.Get(rgSwapchain);
            rpDesc.colorAttachments[0].clearOnLoad   = false;
            rpDesc.depthAttachment.texture           = res.Get(rgDepth);
            rpDesc.depthAttachment.clearOnLoad       = true;
            rpDesc.depthAttachment.clearDepth        = 1.f;
            rpDesc.hasDepth                          = true;
            rpDesc.width  = static_cast<uint32_t>(localW);
            rpDesc.height = static_cast<uint32_t>(localH);

            cmd.BeginRenderPass(rpDesc);
            cmd.SetViewport(RHIViewport{0.f, 0.f, float(localW), float(localH)});
            cmd.SetScissor(RHIScissor{0, 0, uint32_t(localW), uint32_t(localH)});

            for (const auto& item : drawItems) {
                if (!item.pipeline.IsValid()) continue;
                cmd.SetPipeline(item.pipeline);
                cmd.SetDescriptorSet(0, frameDescSet);
                item.material->Bind(&cmd);
                cmd.SetVertexBuffer(0, item.vertexBuffer);
                cmd.SetIndexBuffer(item.indexBuffer);
                if (item.pushConstantSize > 0)
                    cmd.SetPushConstants(&item.worldMatrix, item.pushConstantSize,
                                         RHIShaderStage::Vertex);
                cmd.DrawIndexed(item.indexCount, 1, item.firstIndex,
                                item.vertexOffset, 0);
            }
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
    skyboxProgram.Unload(device.get());
    drawItems.clear();
    matMgr.Shutdown();
    if (gpuBakeOwnsTextures) {
        device->DestroyTexture(brdfLutTex);
        device->DestroyTexture(prefilteredEnvTex);
        device->DestroyTexture(skyboxCubemapTex);
    }
    // Offline-loaded textures (brdfLutTex, prefilteredEnvTex, skyboxCubemapTex) are owned by resMgr.
    device->DestroyTexture(depthTex);
    device->DestroyTexture(whiteTex);
    frameUniforms.Shutdown();
    resMgr.Shutdown();
    device.reset();
    window.reset();
    SA_LOG_INFO("IblDemo: clean shutdown");
    Core::Log::Shutdown();
    return 0;
}
