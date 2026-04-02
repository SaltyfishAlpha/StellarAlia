// IblDemo
//
// Full PBR IBL rendering with GPU-computed IBL maps.
//
// On startup:
//   1. Loads the HDR panorama (RGBA32F) into CPU memory.
//   2. Dispatches three GPU compute shaders (GpuIblBake) to produce the three
//      IBL textures entirely on the GPU — takes ~100 ms vs 30-60 s CPU bake.
//   3. Loads assets/scenes/metal_rough_spheres.sascene.
//   4. Renders with full PBR IBL (Cook-Torrance + diffuse irradiance +
//      specular split-sum) and an equirectangular skybox.

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
#include "resource/loaders/ImageLoader.hpp"
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

    // ── Load HDR panorama from disk (CPU, RGBA32F) ────────────────────────────
    const fs::path hdrPath = assetsDir / "hdri" / "grasslands_sunset_4k.hdr";
    auto hdrOpt = Resource::ImageLoader::LoadHDR(hdrPath.string());
    if (!hdrOpt) {
        SA_LOG_CRITICAL("IblDemo: failed to load HDR '{}'", hdrPath.string());
        Core::Log::Shutdown();
        return 1;
    }
    SA_LOG_INFO("IblDemo: HDR loaded ({}×{})", hdrOpt->width, hdrOpt->height);

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

    // ── GPU IBL bake — runs once before the render loop via ImmediateCompute ──
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

    // Upload the HDR panorama as a separate full-resolution skybox texture.
    // GpuIblBake destroys its internal HDR upload after baking, so we keep our
    // own copy here (binding=4, t_SkyboxMap) for the background pass.
    RHITextureDesc skyboxHdrDesc{};
    skyboxHdrDesc.width     = hdrOpt->width;
    skyboxHdrDesc.height    = hdrOpt->height;
    skyboxHdrDesc.format    = RHIFormat::RGBA32F;
    skyboxHdrDesc.usage     = RHITextureUsage::Sampled;
    skyboxHdrDesc.debugName = "SkyboxHDR";
    RHITextureHandle skyboxHdrTex = device->CreateTexture(skyboxHdrDesc);
    device->UploadTextureData(skyboxHdrTex, hdrOpt->pixelsHDR.data(),
        static_cast<uint64_t>(hdrOpt->width) * hdrOpt->height * 4 * sizeof(float));

    hdrOpt.reset();   // CPU HDR data no longer needed

    frameUniforms.SetIBLTextures(iblResult.brdfLut, iblResult.prefilteredEnv, skyboxHdrTex);
    SA_LOG_INFO("IblDemo: GPU IBL bake complete — textures bound");

    // ── Load scene ────────────────────────────────────────────────────────────
    const fs::path scenePath = assetsDir / "scenes" / "metal_rough_spheres.sascene";
    Scene scene("MetalRoughSpheres");
    if (!SceneSerializer::LoadFromFile(scene, scenePath)) {
        SA_LOG_CRITICAL("IblDemo: failed to load scene '{}'", scenePath.string());
        return 1;
    }
    SA_LOG_INFO("IblDemo: scene loaded");

    scene.UpdateTransforms();

    // ── Resolve meshes + build draw items ─────────────────────────────────────
    std::vector<std::unique_ptr<MaterialInstance>> matInstances;
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

        auto loadTexOrWhite = [&](const AssetID& id) -> RHITextureHandle {
            if (!id.IsValid()) return whiteTex;
            auto h = resMgr.LoadTexture(id);
            return h.IsValid() ? h : whiteTex;
        };

        for (size_t si = 0; si < gpuMesh->subMeshes.size(); ++si) {
            const auto& sub = gpuMesh->subMeshes[si];

            auto inst = matMgr.CreateInstance("PBR");
            if (!inst) continue;

            inst->SetParam<glm::vec4>("baseColorFactor",  sub.baseColorFactor);
            inst->SetParam<float>    ("roughnessFactor",   sub.roughnessFactor);
            inst->SetParam<float>    ("metallicFactor",    sub.metallicFactor);
            inst->SetParam<float>    ("normalScale",       sub.normalScale);
            inst->SetParam<float>    ("occlusionStrength", sub.occlusionStrength);
            inst->SetParam<glm::vec3>("emissiveFactor",    sub.emissiveFactor);

            inst->SetTexture("t_BaseColor",         loadTexOrWhite(sub.baseColorTexture));
            inst->SetTexture("t_Normal",            loadTexOrWhite(sub.normalTexture));
            inst->SetTexture("t_MetallicRoughness", loadTexOrWhite(sub.metallicRoughnessTexture));
            inst->SetTexture("t_Occlusion",         loadTexOrWhite(sub.occlusionTexture));
            inst->SetTexture("t_Emissive",          loadTexOrWhite(sub.emissiveTexture));

            if (const auto* ov = scene.Registry().try_get<MaterialOverrideComponent>(e)) {
                for (const auto& p : ov->params)
                    inst->SetRawParam(p.name, p.value.data(),
                                      static_cast<uint32_t>(p.value.size()));
            }

            DrawItem item{};
            item.vertexBuffer    = gpuMesh->vertexBuffer;
            item.indexBuffer     = gpuMesh->indexBuffer;
            item.firstIndex      = sub.firstIndex;
            item.indexCount      = sub.indexCount;
            item.vertexOffset    = sub.vertexOffset;
            item.material        = inst.get();
            item.worldMatrix     = world.matrix * sub.localTransform;
            item.pushConstantSize = inst->GetType()->shader
                                       .GetMergedReflection().pushConstantSize;

            matInstances.push_back(std::move(inst));
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
        std::copy(std::begin(iblResult.shCoeffs), std::end(iblResult.shCoeffs),
                  std::begin(fu.irrSH));
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
    matInstances.clear();
    matMgr.Shutdown();
    device->DestroyTexture(iblResult.brdfLut);
    device->DestroyTexture(iblResult.prefilteredEnv);
    device->DestroyTexture(skyboxHdrTex);
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
