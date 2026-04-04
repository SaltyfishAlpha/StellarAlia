#include "function/renderer/SceneRenderer.hpp"

#include "core/logs/Log.hpp"
#include "function/material/AttachmentKey.hpp"
#include "function/material/MaterialType.hpp"
#include "function/scene/Components.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/cook/CookedSH9.hpp"
#include "resource/cook/CookedTexture.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>

#include <algorithm>
#include <filesystem>

namespace StellarAlia {

// ── Init / Shutdown ───────────────────────────────────────────────────────────

bool SceneRenderer::Init(const Desc& desc) {
    m_device       = desc.device;
    m_matMgr       = desc.matMgr;
    m_resMgr       = desc.resMgr;
    m_shaderDir    = desc.shaderDir;
    m_cookCacheDir = desc.cookCacheDir;
    m_config       = desc.config;

    // Clamp bloom mip count to valid range.
    m_bloomMipCount = std::max(2, std::min(m_config.bloomMipCount, kMaxBloomMips));

    // ── FrameUniformsBuffer (owned — provides frameLayout for all shaders) ─────
    m_frameUniforms.Init(desc.device);
    const auto frameLayout = m_frameUniforms.GetLayout();

    // IBL bake — initialise so SetIBL can fall back to GPU bake on cache miss.
    if (!m_iblBake.Init(desc.device, desc.shaderDir))
        SA_LOG_WARN("SceneRenderer: GpuIblBake init failed — IBL bake unavailable");

    // LTC LUT upload — always succeeds if device is valid; data is embedded.
    m_ltcBake.Upload(desc.device);
    m_frameUniforms.SetLtcTextures(m_ltcBake.GetLtcMat(), m_ltcBake.GetLtcAmp());

    // ── Build FeatureInitContext (shared by Init and all feature OnInits) ────────
    const FeatureInitContext ctx{desc.device, desc.matMgr, desc.resMgr,
                                 frameLayout, desc.shaderDir};

    // ── Shadow map (fixed size from config, never resized) ───────────────────
    {
        const uint32_t smSize = m_config.shadowEnabled ? m_config.shadowMapSize : 1u;
        RHI::RHITextureDesc d{};
        d.width     = smSize;
        d.height    = smSize;
        d.format    = RHI::RHIFormat::D32F;
        d.usage     = RHI::RHITextureUsage::DepthStencil
                    | RHI::RHITextureUsage::Sampled;
        d.debugName = "ShadowMap";
        m_shadowMap = desc.device->CreateTexture(d);
    }

    // ── G-Buffer + HDR textures (1×1 placeholders, resized on first RenderFrame) ─
    {
        auto makeRT = [&](RHI::RHIFormat fmt, const char* name) {
            RHI::RHITextureDesc d{};
            d.width     = 1;
            d.height    = 1;
            d.format    = fmt;
            d.usage     = RHI::RHITextureUsage::RenderTarget
                        | RHI::RHITextureUsage::Sampled;
            d.debugName = name;
            return desc.device->CreateTexture(d);
        };
        m_gbRT0      = makeRT(RHI::RHIFormat::RGBA8_UNORM, "GBuffer_RT0");
        m_gbRT1      = makeRT(RHI::RHIFormat::RGBA16F,     "GBuffer_RT1");
        m_gbRT2      = makeRT(RHI::RHIFormat::RGBA16F,     "GBuffer_RT2");
        m_hdrTex     = makeRT(RHI::RHIFormat::RGBA16F,     "HDR_Color");
        // Bloom placeholders — only allocate for active mip levels.
        for (int i = 0; i < m_bloomMipCount; ++i) {
            const std::string name = "Bloom" + std::to_string(i);
            m_bloomMip[i]  = makeRT(RHI::RHIFormat::RGBA16F, name.c_str());
            m_bloomMipW[i] = m_bloomMipH[i] = 1;
        }
        m_gbWidth = m_gbHeight = 1;
    }

    // ── Depth texture (1×1 placeholder, resized on first RenderFrame) ─────────
    {
        RHI::RHITextureDesc d{};
        d.width     = 1;
        d.height    = 1;
        d.format    = RHI::RHIFormat::D32F;
        d.usage     = RHI::RHITextureUsage::DepthStencil
                    | RHI::RHITextureUsage::Sampled;
        d.debugName = "SceneDepth";
        m_depthTex    = desc.device->CreateTexture(d);
        m_depthWidth  = 1;
        m_depthHeight = 1;
    }

    // ── Pre-register built-in passes as features ───────────────────────────────
    // Insert at front in reverse execution order so final order is:
    //   [Shadow?, Skybox, GBuffer, DeferredLighting, Bloom?, Tonemap?, ...user features]
    if (m_config.builtinTonemap)
        m_features.insert(m_features.begin(), std::make_unique<TonemapFeature>());
    if (m_config.bloomEnabled)
        m_features.insert(m_features.begin(), std::make_unique<BloomFeature>(m_bloomMipCount));
    m_features.insert(m_features.begin(), std::make_unique<DeferredLightingFeature>());
    m_features.insert(m_features.begin(), std::make_unique<GBufferFeature>(this));
    m_features.insert(m_features.begin(), std::make_unique<SkyboxFeature>());
    if (m_config.shadowEnabled)
        m_features.insert(m_features.begin(), std::make_unique<ShadowFeature>());

    // ── Call OnInit on all features (built-in + user pre-registered) ──────────
    for (auto& f : m_features)
        f->OnInit(ctx);

    m_ready = true;
    SA_LOG_INFO("SceneRenderer: initialized (deferred pipeline)");
    return true;
}

void SceneRenderer::Shutdown() {
    for (auto& f : m_features)
        f->OnShutdown(m_device);
    m_features.clear();

    m_drawItems.clear();
    m_defaultMaterial.reset();

    if (m_shadowMap.IsValid())   m_device->DestroyTexture(m_shadowMap);
    if (m_gbRT0.IsValid())       m_device->DestroyTexture(m_gbRT0);
    if (m_gbRT1.IsValid())       m_device->DestroyTexture(m_gbRT1);
    if (m_gbRT2.IsValid())       m_device->DestroyTexture(m_gbRT2);
    if (m_hdrTex.IsValid())      m_device->DestroyTexture(m_hdrTex);
    for (int i = 0; i < m_bloomMipCount; ++i)
        if (m_bloomMip[i].IsValid()) m_device->DestroyTexture(m_bloomMip[i]);

    if (m_iblBake.IsInitialized())
        m_iblBake.Shutdown(m_device);
    if (m_ltcBake.IsUploaded())
        m_ltcBake.Shutdown(m_device);
    if (m_depthTex.IsValid())
        m_device->DestroyTexture(m_depthTex);

    m_frameUniforms.Shutdown();
    m_ready = false;
}

// ── SetIBL ────────────────────────────────────────────────────────────────────

bool SceneRenderer::SetIBL(const WorldSettings& ws)
{
    for (int i = 0; i < 9; ++i) m_shCoeffs[i] = {};

    const bool hasOffline = ws.sh9.IsValid() &&
                            ws.prefilteredEnv.IsValid() &&
                            ws.brdfLut.IsValid() &&
                            ws.skyboxCubemap.IsValid();
    if (hasOffline) {
        auto sh9Opt = m_resMgr->LoadSH9Coeffs(ws.sh9);
        auto blt    = m_resMgr->LoadTexture(ws.brdfLut);
        auto pet    = m_resMgr->LoadTexture(ws.prefilteredEnv);
        auto sky    = m_resMgr->LoadTexture(ws.skyboxCubemap);

        if (sh9Opt && blt.IsValid() && pet.IsValid() && sky.IsValid()) {
            for (int i = 0; i < 9; ++i) m_shCoeffs[i] = (*sh9Opt)[i];
            m_frameUniforms.SetIBLTextures(blt, pet, sky);
            SA_LOG_INFO("SceneRenderer: IBL loaded from cook cache");
            return true;
        }
        SA_LOG_WARN("SceneRenderer: offline IBL incomplete, falling back to GPU bake");
    }

    if (!ws.skyboxHdr.IsValid()) {
        SA_LOG_WARN("SceneRenderer: no skybox HDR in WorldSettings — IBL skipped");
        return false;
    }

    auto hdrOpt = m_resMgr->LoadHDRImageData(ws.skyboxHdr);
    if (!hdrOpt) {
        SA_LOG_ERROR("SceneRenderer: failed to load skybox HDR ({})",
                     ws.skyboxHdr.ToString());
        return false;
    }

    if (!m_iblBake.IsInitialized()) {
        SA_LOG_ERROR("SceneRenderer: GpuIblBake not initialised — IBL unavailable");
        return false;
    }

    const GpuIblBake::Result r = m_iblBake.Bake(m_device, *hdrOpt);
    if (!r.IsValid()) {
        SA_LOG_ERROR("SceneRenderer: GpuIblBake failed");
        return false;
    }
    SA_LOG_INFO("SceneRenderer: GPU IBL bake complete");

    std::copy(std::begin(r.shCoeffs), std::end(r.shCoeffs), std::begin(m_shCoeffs));

    m_frameUniforms.SetIBLTextures(r.brdfLut, r.prefilteredEnv, r.skyboxCubemap);

    auto saveGpuTex = [&](RHI::RHITextureHandle tex,
                           const AssetID&        id,
                           bool                  isCubemap) {
        if (!id.IsValid()) return;
        const RHI::RHITextureDesc* desc = m_device->GetTextureDesc(tex);
        if (!desc) return;

        const uint32_t bytesPerPixel = 4 * sizeof(float);
        const uint32_t mipCount      = desc->mipLevels;
        const uint32_t numLayers     = isCubemap ? 6u : 1u;

        std::vector<std::vector<uint8_t>>         mipData(mipCount);
        std::vector<RHI::IRHIDevice::MipReadback> readbacks(mipCount);
        for (uint32_t m = 0; m < mipCount; ++m) {
            const uint32_t mW = std::max(1u, desc->width  >> m);
            const uint32_t mH = std::max(1u, desc->height >> m);
            const uint64_t sz = static_cast<uint64_t>(mW) * mH * bytesPerPixel * numLayers;
            mipData[m].resize(sz);
            readbacks[m] = { mipData[m].data(), sz };
        }
        m_device->ReadbackTextureMips(tex, readbacks);

        Resource::CookedTexture cooked;
        cooked.id        = id;
        cooked.width     = desc->width;
        cooked.height    = desc->height;
        cooked.mipLevels = mipCount;
        cooked.format    = Resource::CookedTextureFormat::RGBA32F;
        cooked.srgb      = false;
        cooked.isHDR     = true;
        cooked.cubemap   = isCubemap;
        uint32_t offset  = 0;
        for (uint32_t m = 0; m < mipCount; ++m) {
            cooked.mips.push_back({offset, static_cast<uint32_t>(mipData[m].size())});
            cooked.data.insert(cooked.data.end(), mipData[m].begin(), mipData[m].end());
            offset += static_cast<uint32_t>(mipData[m].size());
        }
        const std::string path = m_cookCacheDir + "/" + id.ToString() + ".satex";
        if (Resource::SaveCookedTexture(cooked, path))
            SA_LOG_INFO("SceneRenderer: cached IBL texture → {}.satex",
                        id.ToString().substr(0, 8));
    };

    saveGpuTex(r.brdfLut,        ws.brdfLut,        false);
    saveGpuTex(r.prefilteredEnv, ws.prefilteredEnv, true);
    saveGpuTex(r.skyboxCubemap,  ws.skyboxCubemap,  true);

    if (ws.sh9.IsValid()) {
        Resource::CookedSH9 sh9Cache;
        sh9Cache.id = ws.sh9;
        for (int i = 0; i < 9; ++i) sh9Cache.coeffs[i] = r.shCoeffs[i];
        const std::string sh9Path = m_cookCacheDir + "/" + ws.sh9.ToString() + ".sash9";
        if (Resource::SaveCookedSH9(sh9Cache, sh9Path))
            SA_LOG_INFO("SceneRenderer: cached SH9 → {}.sash9",
                        ws.sh9.ToString().substr(0, 8));
    }
    return true;
}

// ── BuildDrawList ─────────────────────────────────────────────────────────────

void SceneRenderer::BuildDrawList(Scene& scene) {
    m_drawItems.clear();

    if (!m_defaultMaterial) {
        SA_LOG_ERROR("SceneRenderer::BuildDrawList: no default material (Init failed?)");
        return;
    }

    // G-Buffer attachment key: 3 MRT + depth.
    // All geometry is rendered through the same deferred_geometry shader pair.
    AttachmentKey gbKey{};
    gbKey.colorCount      = 3;
    gbKey.colorFormats[0] = RHI::RHIFormat::RGBA8_UNORM;
    gbKey.colorFormats[1] = RHI::RHIFormat::RGBA16F;
    gbKey.colorFormats[2] = RHI::RHIFormat::RGBA16F;
    gbKey.depthFormat     = RHI::RHIFormat::D32F;

    const auto& reg = scene.Registry();

    scene.View<StaticMeshComponent, WorldTransformComponent>().each(
        [&](entt::entity e,
            const StaticMeshComponent&     meshComp,
            const WorldTransformComponent& /*wt*/)
    {
        if (!meshComp.meshAsset.IsValid()) return;

        const Resource::GPUMesh* gpuMesh = m_resMgr->LoadMesh(meshComp.meshAsset);
        if (!gpuMesh) {
            SA_LOG_WARN("SceneRenderer: mesh {} not in cook cache",
                        meshComp.meshAsset.ToString());
            return;
        }

        const auto* pbrComp   = reg.try_get<PBRSurfaceComponent>(e);
        const auto* paramComp = reg.try_get<MaterialParamComponent>(e);

        for (size_t si = 0; si < gpuMesh->subMeshes.size(); ++si) {
            const auto& sub = gpuMesh->subMeshes[si];

            MaterialInstance* base = m_defaultMaterial.get();

            if (si < meshComp.materialSlots.size() && meshComp.materialSlots[si].IsValid()) {
                MaterialInstance* loaded = m_matMgr->LoadMaterial(
                    meshComp.materialSlots[si], m_cookCacheDir, *m_resMgr);
                if (loaded) base = loaded;
            } else if (sub.defaultMaterialID.IsValid()) {
                MaterialInstance* loaded = m_matMgr->LoadMaterial(
                    sub.defaultMaterialID, m_cookCacheDir, *m_resMgr);
                if (loaded) base = loaded;
            }

            DrawItem item{};
            item.entity            = e;
            item.subLocalTransform = sub.localTransform;
            item.vertexBuffer      = gpuMesh->vertexBuffer;
            item.indexBuffer       = gpuMesh->indexBuffer;
            item.firstIndex        = sub.firstIndex;
            item.indexCount        = sub.indexCount;
            item.vertexOffset      = sub.vertexOffset;

            if (pbrComp || paramComp) {
                auto clone = m_matMgr->CloneInstance(base);
                if (!clone) { item.material = base; }
                else {
                    if (pbrComp) {
                        clone->SetVec4 ("baseColorFactor", pbrComp->baseColor);
                        clone->SetFloat("roughnessFactor", pbrComp->roughness);
                        clone->SetFloat("metallicFactor",  pbrComp->metallic);
                        if (pbrComp->albedoMap.IsValid())
                            clone->SetTexture("t_BaseColor",
                                m_resMgr->LoadTexture(pbrComp->albedoMap));
                        if (pbrComp->normalMap.IsValid())
                            clone->SetTexture("t_Normal",
                                m_resMgr->LoadTexture(pbrComp->normalMap));
                    }
                    if (paramComp) {
                        for (const auto& [name, val] : paramComp->scalars)
                            std::visit([&](const auto& v){ clone->SetParam(name, v); }, val);
                        for (const auto& [name, texID] : paramComp->textures)
                            if (texID.IsValid())
                                clone->SetTexture(name, m_resMgr->LoadTexture(texID));
                    }
                    item.material      = clone.get();
                    item.ownedMaterial = std::move(clone);
                }
            } else {
                item.material = base;
            }

            // Each material type owns its own G-Buffer pipeline.
            // The attachment key (3 MRT + depth) is the same for all types;
            // the pipeline is keyed per-type so different set=1 layouts are safe.
            item.pipeline         = item.material->GetType()->GetOrCreatePipeline(m_device, gbKey);
            item.pushConstantSize = static_cast<uint32_t>(sizeof(glm::mat4));

            m_drawItems.push_back(std::move(item));
        }
    });

    SA_LOG_INFO("SceneRenderer: built {} draw item(s)", m_drawItems.size());
}

// ── GatherLights ──────────────────────────────────────────────────────────────

LightUniforms SceneRenderer::GatherLights(const Scene& scene) const {
    LightUniforms lu{};
    int idx = 0;

    scene.View<DirectionalLightComponent, TransformComponent>().each(
        [&](auto, const DirectionalLightComponent& dl, const TransformComponent& t) {
            if (idx >= LightUniforms::MAX_LIGHTS) return;
            auto& e     = lu.lights[idx++];
            e.direction = glm::normalize(t.rotation * glm::vec3(0.f, 0.f, -1.f));
            e.color     = dl.color;
            e.intensity = dl.intensity;
            e.type      = 0;
        });

    scene.View<PointLightComponent, WorldTransformComponent>().each(
        [&](auto, const PointLightComponent& pl, const WorldTransformComponent& wt) {
            if (idx >= LightUniforms::MAX_LIGHTS) return;
            auto& e     = lu.lights[idx++];
            e.position  = glm::vec3(wt.matrix[3]);
            e.color     = pl.color;
            e.intensity = pl.intensity;
            e.range     = pl.range;
            e.type      = 1;
        });

    scene.View<SpotLightComponent, TransformComponent, WorldTransformComponent>().each(
        [&](auto, const SpotLightComponent& sl,
            const TransformComponent& t, const WorldTransformComponent& wt) {
            if (idx >= LightUniforms::MAX_LIGHTS) return;
            auto& e      = lu.lights[idx++];
            e.position   = glm::vec3(wt.matrix[3]);
            e.direction  = glm::normalize(t.rotation * glm::vec3(0.f, 0.f, -1.f));
            e.color      = sl.color;
            e.intensity  = sl.intensity;
            e.range      = sl.range;
            e.innerAngle = sl.innerAngle;
            e.outerAngle = sl.outerAngle;
            e.type       = 2;
        });

    scene.View<AreaLightComponent, TransformComponent, WorldTransformComponent>().each(
        [&](auto, const AreaLightComponent& al,
            const TransformComponent& /*t*/, const WorldTransformComponent& wt) {
            if (idx >= LightUniforms::MAX_LIGHTS) return;
            auto& e       = lu.lights[idx++];
            e.position    = glm::vec3(wt.matrix[3]);
            e.color       = al.color;
            e.intensity   = al.intensity;
            e.innerAngle  = al.size.x;
            e.outerAngle  = al.size.y;
            e.tangentU    = glm::normalize(glm::vec3(wt.matrix[0]));
            e.tangentV    = glm::normalize(glm::vec3(wt.matrix[2]));
            e.type        = 3;
        });

    lu.lightCount = idx;
    return lu;
}

// ── FillCameraUniforms ────────────────────────────────────────────────────────

void SceneRenderer::FillCameraUniforms(const Scene& scene,
                                        int vpWidth, int vpHeight,
                                        FrameUniforms& fu) const {
    scene.View<CameraComponent, ActiveCameraTag, WorldTransformComponent>().each(
        [&](auto, const CameraComponent& cam, const WorldTransformComponent& wt) {
            const float aspect = (vpHeight > 0)
                ? static_cast<float>(vpWidth) / static_cast<float>(vpHeight)
                : 1.f;
            fu.view        = glm::inverse(wt.matrix);
            fu.proj        = glm::perspective(cam.fovY, aspect, cam.nearPlane, cam.farPlane);
            fu.proj[1][1] *= -1.f;
            fu.viewProj    = fu.proj * fu.view;
            fu.invViewProj = glm::inverse(fu.viewProj);
            fu.cameraPos   = glm::vec3(wt.matrix[3]);
        });
}

// ── AddFeature ────────────────────────────────────────────────────────────────

void SceneRenderer::AddFeature(std::unique_ptr<RenderFeature> feature) {
    if (m_ready)
        feature->OnInit({m_device, m_matMgr, m_resMgr, m_frameUniforms.GetLayout(), m_shaderDir});
    m_features.push_back(std::move(feature));
}

// ── RenderFrame ───────────────────────────────────────────────────────────────

void SceneRenderer::RenderFrame(Scene& scene, uint32_t w, uint32_t h)
{
    // ── Resize G-Buffer + depth if viewport changed ────────────────────────────
    if (w != m_depthWidth || h != m_depthHeight) {
        m_device->WaitIdle();

        auto recreateRT = [&](RHI::RHITextureHandle& tex,
                               RHI::RHIFormat fmt, const char* name) {
            if (tex.IsValid()) m_device->DestroyTexture(tex);
            RHI::RHITextureDesc d{};
            d.width     = w;
            d.height    = h;
            d.format    = fmt;
            d.usage     = RHI::RHITextureUsage::RenderTarget
                        | RHI::RHITextureUsage::Sampled;
            d.debugName = name;
            tex = m_device->CreateTexture(d);
        };

        recreateRT(m_gbRT0,       RHI::RHIFormat::RGBA8_UNORM, "GBuffer_RT0");
        recreateRT(m_gbRT1,       RHI::RHIFormat::RGBA16F,     "GBuffer_RT1");
        recreateRT(m_gbRT2,       RHI::RHIFormat::RGBA16F,     "GBuffer_RT2");
        recreateRT(m_hdrTex,      RHI::RHIFormat::RGBA16F,     "HDR_Color");
        m_gbWidth  = w;
        m_gbHeight = h;

        // Bloom pyramid: mip[i] = 1/(2^(i+1)) resolution, active levels only.
        for (int i = 0; i < m_bloomMipCount; ++i) {
            const uint32_t mw = std::max(1u, w >> (i + 1));
            const uint32_t mh = std::max(1u, h >> (i + 1));
            if (m_bloomMip[i].IsValid()) m_device->DestroyTexture(m_bloomMip[i]);
            const std::string name = "Bloom" + std::to_string(i);
            RHI::RHITextureDesc d{};
            d.width     = mw; d.height = mh;
            d.format    = RHI::RHIFormat::RGBA16F;
            d.usage     = RHI::RHITextureUsage::RenderTarget
                        | RHI::RHITextureUsage::Sampled;
            d.debugName = name.c_str();
            m_bloomMip[i]  = m_device->CreateTexture(d);
            m_bloomMipW[i] = mw;
            m_bloomMipH[i] = mh;
        }

        {
            if (m_depthTex.IsValid()) m_device->DestroyTexture(m_depthTex);
            RHI::RHITextureDesc d{};
            d.width     = w;
            d.height    = h;
            d.format    = RHI::RHIFormat::D32F;
            d.usage     = RHI::RHITextureUsage::DepthStencil
                        | RHI::RHITextureUsage::Sampled;
            d.debugName = "SceneDepth";
            m_depthTex = m_device->CreateTexture(d);
        }
        m_depthWidth  = w;
        m_depthHeight = h;
    }

    // ── Phase 1: collect frame data ───────────────────────────────────────────
    FrameUniforms fu{};
    fu.resolution = {static_cast<float>(w), static_cast<float>(h)};
    fu.time       = static_cast<float>(m_frameCount) / 60.f;
    std::copy(std::begin(m_shCoeffs), std::end(m_shCoeffs), std::begin(fu.irrSH));
    FillCameraUniforms(scene, static_cast<int>(w), static_cast<int>(h), fu);
    const LightUniforms lu = GatherLights(scene);

    // Compute light-space matrix from the first directional light found.
    // Orthographic frustum covers a fixed 60×60×100 world-space volume.
    fu.lightSpaceMatrix = glm::mat4(1.0f);
    fu.shadowBias       = 0.001f;
    for (int i = 0; i < lu.lightCount; ++i) {
        if (lu.lights[i].type == 0) {
            const glm::vec3 dir = lu.lights[i].direction;
            const glm::vec3 up  = (glm::abs(dir.y) < 0.99f)
                                ? glm::vec3(0.f, 1.f, 0.f)
                                : glm::vec3(1.f, 0.f, 0.f);
            glm::mat4 lv = glm::lookAt(-dir * 50.f, glm::vec3(0.f), up);
            glm::mat4 lp = glm::orthoZO(-30.f, 30.f, -30.f, 30.f, 1.f, 100.f);
            lp[1][1] *= -1.f;  // Vulkan Y-flip
            fu.lightSpaceMatrix = lp * lv;
            break;
        }
    }

    // ── Phase 2: GPU work ─────────────────────────────────────────────────────
    m_cmd = m_device->BeginFrame();
    if (!m_cmd) return;

    const uint32_t fi = m_device->GetCurrentFrameIndex();
    m_frameUniforms.Upload(fi, fu, lu);
    m_frameDescSet = m_frameUniforms.GetDescriptorSet(fi);
    m_frameWidth   = w;
    m_frameHeight  = h;

    // ── Reset RenderGraph, import all textures ────────────────────────────────
    m_rg.Reset();
    m_rgSwapchain = m_rg.ImportTexture("Swapchain",
        m_device->GetSwapchainTexture(),
        RHI::RHIResourceState::RenderTarget, RHI::RHIResourceState::RenderTarget);
    m_rgDepth = m_rg.ImportTexture("Depth",
        m_depthTex,
        RHI::RHIResourceState::Undefined, RHI::RHIResourceState::Undefined);
    m_rgGbRT0 = m_rg.ImportTexture("GBuffer_RT0", m_gbRT0,
        RHI::RHIResourceState::Undefined, RHI::RHIResourceState::Undefined);
    m_rgGbRT1 = m_rg.ImportTexture("GBuffer_RT1", m_gbRT1,
        RHI::RHIResourceState::Undefined, RHI::RHIResourceState::Undefined);
    m_rgGbRT2 = m_rg.ImportTexture("GBuffer_RT2", m_gbRT2,
        RHI::RHIResourceState::Undefined, RHI::RHIResourceState::Undefined);
    m_rgHdr = m_rg.ImportTexture("HDR_Color", m_hdrTex,
        RHI::RHIResourceState::Undefined, RHI::RHIResourceState::Undefined);
    m_rgShadowMap = m_rg.ImportTexture("ShadowMap", m_shadowMap,
        RHI::RHIResourceState::Undefined, RHI::RHIResourceState::Undefined);
    for (int i = 0; i < m_bloomMipCount; ++i) {
        const std::string name = "Bloom" + std::to_string(i);
        m_rgBloomMip[i] = m_rg.ImportTexture(name, m_bloomMip[i],
            RHI::RHIResourceState::Undefined, RHI::RHIResourceState::Undefined);
    }

    // ── Build RendererHandles (RG handles for all built-in render targets) ───────
    RendererHandles handles{};
    handles.hdr        = m_rgHdr;
    handles.swapchain  = m_rgSwapchain;
    handles.depth      = m_rgDepth;
    handles.gbufferRT0 = m_rgGbRT0;
    handles.gbufferRT1 = m_rgGbRT1;
    handles.gbufferRT2 = m_rgGbRT2;
    handles.shadowMap  = m_rgShadowMap;
    handles.bloomMipCount = m_bloomMipCount;
    for (int i = 0; i < m_bloomMipCount; ++i)
        handles.bloomMip[i] = m_rgBloomMip[i];

    // ── Build RHI lookup table for FrameContext::BindTexture ──────────────────
    // Indexed by RGTextureHandle.index (assigned sequentially by ImportTexture).
    // We build a flat array: index → RHITextureHandle.
    // RG assigns indices starting from 0; the table size = highest index + 1.
    // We pre-fill with the known mapping here; unknown indices resolve to {}.
    const uint32_t tableSize = 8 + static_cast<uint32_t>(kMaxBloomMips);
    std::vector<RHI::RHITextureHandle> rhiTable(tableSize);
    auto fillEntry = [&](RGTextureHandle rg, RHI::RHITextureHandle rhi) {
        if (rg.IsValid() && rg.index < tableSize) rhiTable[rg.index] = rhi;
    };
    fillEntry(m_rgSwapchain,  m_device->GetSwapchainTexture());
    fillEntry(m_rgDepth,      m_depthTex);
    fillEntry(m_rgGbRT0,      m_gbRT0);
    fillEntry(m_rgGbRT1,      m_gbRT1);
    fillEntry(m_rgGbRT2,      m_gbRT2);
    fillEntry(m_rgHdr,        m_hdrTex);
    fillEntry(m_rgShadowMap,  m_shadowMap);
    for (int i = 0; i < m_bloomMipCount; ++i)
        fillEntry(m_rgBloomMip[i], m_bloomMip[i]);

    FrameContext ctx{};
    ctx.rg       = &m_rg;
    ctx.frameSet = m_frameDescSet;
    ctx.device   = m_device;
    ctx.m_rhiTable = rhiTable.data();
    ctx.m_tableSize = tableSize;

    // ── All passes: [Shadow, Skybox, GBuffer, DeferredLighting, Bloom, Tonemap] ─
    for (auto& f : m_features)
        f->AddPasses(*this, ctx, handles, scene.Registry(), w, h);

    // ── Compile + Execute + Present ───────────────────────────────────────────
    m_rg.Compile();
    m_rg.Execute(*m_device, *m_cmd);

    m_device->EndFrame();
    m_device->Present();

    m_cmd = nullptr;
    ++m_frameCount;
}

// ── FrameContext::BindTexture ─────────────────────────────────────────────────

void FrameContext::BindTexture(RHI::RHIDescSetHandle set, uint32_t binding,
                               RGTextureHandle handle) const
{
    if (!handle.IsValid() || handle.index >= m_tableSize) return;
    const RHI::RHITextureHandle rhi = m_rhiTable[handle.index];
    if (!rhi.IsValid()) return;
    device->WriteDescriptorTexture(set, binding, rhi);
}

// ── ShadowFeature ─────────────────────────────────────────────────────────────

void SceneRenderer::ShadowFeature::OnInit(const FeatureInitContext& ctx)
{
    ctx.matMgr->RegisterTypeFromShaders(
        {"Shadow", "shadow", "shadow",
         RHI::RHICullMode::Front, RHI::RHIBlendMode::Opaque, true, true, false}, ctx);
    m_type = ctx.matMgr->GetType("Shadow");
    if (!m_type) SA_LOG_WARN("ShadowFeature: shader load failed — shadows disabled");
}

void SceneRenderer::ShadowFeature::AddPasses(SceneRenderer& renderer,
                                              const FrameContext& ctx,
                                              const RendererHandles& handles,
                                              const entt::registry& reg,
                                              uint32_t /*w*/, uint32_t /*h*/)
{
    if (!m_type) return;

    AttachmentKey shadowKey{};
    shadowKey.colorCount  = 0;
    shadowKey.depthFormat = RHI::RHIFormat::D32F;

    const RHI::RHIPipelineHandle pipeline = m_type->GetOrCreatePipeline(ctx.device, shadowKey);
    if (!pipeline.IsValid()) return;

    const RHI::RHIDescSetHandle frameSet    = ctx.frameSet;
    const RGTextureHandle       rgShadowMap = handles.shadowMap;
    constexpr uint32_t          kSize       = 2048;

    ctx.rg->AddPass("Shadow",
        [rgShadowMap](RGPassBuilder& b) { b.WriteDepth(rgShadowMap); },
        [&drawItems = renderer.m_drawItems, &reg, pipeline, frameSet, rgShadowMap]
        (RHI::IRHICommandList& cmd, const RGResources& res)
        {
            RHI::RHIRenderPassDesc rpDesc{};
            rpDesc.colorAttachmentCount        = 0;
            rpDesc.depthAttachment.texture     = res.Get(rgShadowMap);
            rpDesc.depthAttachment.clearOnLoad = true;
            rpDesc.depthAttachment.clearDepth  = 1.f;
            rpDesc.hasDepth = true;
            rpDesc.width    = kSize;
            rpDesc.height   = kSize;

            cmd.BeginRenderPass(rpDesc);
            cmd.SetViewport(RHI::RHIViewport{0.f, 0.f, float(kSize), float(kSize)});
            cmd.SetScissor(RHI::RHIScissor{0, 0, kSize, kSize});

            for (const auto& item : drawItems) {
                if (!item.pipeline.IsValid()) continue;
                const auto* wt = reg.try_get<WorldTransformComponent>(item.entity);
                if (!wt) continue;
                const glm::mat4 world = wt->matrix * item.subLocalTransform;

                cmd.SetPipeline(pipeline);
                cmd.SetDescriptorSet(0, frameSet);
                cmd.SetVertexBuffer(0, item.vertexBuffer);
                cmd.SetIndexBuffer(item.indexBuffer);
                cmd.SetPushConstants(&world, sizeof(glm::mat4),
                                     RHI::RHIShaderStage::Vertex);
                cmd.DrawIndexed(item.indexCount, 1, item.firstIndex,
                                item.vertexOffset, 0);
            }
            cmd.EndRenderPass();
        });
}

// ── SkyboxFeature ─────────────────────────────────────────────────────────────

void SceneRenderer::SkyboxFeature::OnInit(const FeatureInitContext& ctx)
{
    ctx.matMgr->RegisterTypeFromShaders(
        {"Skybox", "skybox", "skybox",
         RHI::RHICullMode::None, RHI::RHIBlendMode::Opaque, false, false, true}, ctx);
    m_type = ctx.matMgr->GetType("Skybox");
    if (!m_type) SA_LOG_WARN("SkyboxFeature: shader load failed — skybox disabled");
}

void SceneRenderer::SkyboxFeature::AddPasses(SceneRenderer& /*renderer*/,
                                              const FrameContext& ctx,
                                              const RendererHandles& handles,
                                              const entt::registry& /*reg*/,
                                              uint32_t w, uint32_t h)
{
    if (!m_type) return;

    // Skybox writes to HDR buffer (not swapchain); tonemap handles the final blit.
    AttachmentKey key{};
    key.colorCount      = 1;
    key.colorFormats[0] = RHI::RHIFormat::RGBA16F;
    key.depthFormat     = RHI::RHIFormat::Undefined;

    const RHI::RHIPipelineHandle pipeline = m_type->GetOrCreatePipeline(ctx.device, key);

    const RHI::RHIDescSetHandle descSet = ctx.frameSet;
    const RGTextureHandle rgHdr = handles.hdr;

    ctx.rg->AddPass("Skybox",
        [rgHdr](RGPassBuilder& b) { b.Write(rgHdr); },
        [pipeline, descSet, rgHdr, w, h]
        (RHI::IRHICommandList& cmd, const RGResources& res)
        {
            RHI::RHIRenderPassDesc rpDesc{};
            rpDesc.colorAttachmentCount = 1;
            rpDesc.colorAttachments[0].texture     = res.Get(rgHdr);
            rpDesc.colorAttachments[0].clearOnLoad = true;
            rpDesc.width  = w;
            rpDesc.height = h;

            cmd.BeginRenderPass(rpDesc);
            cmd.SetViewport(RHI::RHIViewport{0.f, 0.f, float(w), float(h)});
            cmd.SetScissor(RHI::RHIScissor{0, 0, w, h});
            cmd.SetPipeline(pipeline);
            cmd.SetDescriptorSet(0, descSet);
            cmd.Draw(3, 1, 0, 0);
            cmd.EndRenderPass();
        });
}

// ── GBufferFeature ────────────────────────────────────────────────────────────

void SceneRenderer::GBufferFeature::OnInit(const FeatureInitContext& ctx)
{
    // pbr.frag is a forward shader (single output); deferred_geometry.frag writes
    // albedo/normal/metallic-roughness to 3 MRTs — use that for the G-buffer pass.
    if (!ctx.matMgr->RegisterTypeFromShaders(
            {"PBR", "deferred_geometry", "deferred_geometry"}, ctx)) {
        SA_LOG_ERROR("GBufferFeature: failed to register PBR material type");
        return;
    }

    m_owner->m_defaultMaterial = ctx.matMgr->CreateInstance("PBR");
    if (m_owner->m_defaultMaterial) {
        m_owner->m_defaultMaterial->SetVec4 ("baseColorFactor",  {0.8f, 0.8f, 0.8f, 1.0f});
        m_owner->m_defaultMaterial->SetFloat("roughnessFactor",   0.8f);
        m_owner->m_defaultMaterial->SetFloat("metallicFactor",    0.0f);
        m_owner->m_defaultMaterial->SetFloat("normalScale",       1.0f);
        m_owner->m_defaultMaterial->SetFloat("occlusionStrength", 1.0f);
        m_owner->m_defaultMaterial->SetVec3 ("emissiveFactor",   {0.0f, 0.0f, 0.0f});
    }
}

void SceneRenderer::GBufferFeature::AddPasses(SceneRenderer& renderer,
                                               const FrameContext& ctx,
                                               const RendererHandles& handles,
                                               const entt::registry& reg,
                                               uint32_t w, uint32_t h)
{
    const RGTextureHandle rgRT0   = handles.gbufferRT0;
    const RGTextureHandle rgRT1   = handles.gbufferRT1;
    const RGTextureHandle rgRT2   = handles.gbufferRT2;
    const RGTextureHandle rgDepth = handles.depth;
    const RHI::RHIDescSetHandle frameSet = ctx.frameSet;
    const entt::registry* regPtr = &reg;

    ctx.rg->AddPass("GBuffer",
        [rgRT0, rgRT1, rgRT2, rgDepth](RGPassBuilder& b) {
            b.Write(rgRT0);
            b.Write(rgRT1);
            b.Write(rgRT2);
            b.WriteDepth(rgDepth);
        },
        [&drawItems = renderer.m_drawItems, regPtr, frameSet, w, h,
         rgRT0, rgRT1, rgRT2, rgDepth]
        (RHI::IRHICommandList& cmd, const RGResources& res)
        {
            RHI::RHIRenderPassDesc rpDesc{};
            rpDesc.colorAttachmentCount = 3;
            rpDesc.colorAttachments[0].texture     = res.Get(rgRT0);
            rpDesc.colorAttachments[0].clearOnLoad = true;
            rpDesc.colorAttachments[1].texture     = res.Get(rgRT1);
            rpDesc.colorAttachments[1].clearOnLoad = true;
            rpDesc.colorAttachments[2].texture     = res.Get(rgRT2);
            rpDesc.colorAttachments[2].clearOnLoad = true;
            rpDesc.depthAttachment.texture     = res.Get(rgDepth);
            rpDesc.depthAttachment.clearOnLoad = true;
            rpDesc.depthAttachment.clearDepth  = 1.f;
            rpDesc.hasDepth = true;
            rpDesc.width    = w;
            rpDesc.height   = h;

            cmd.BeginRenderPass(rpDesc);
            cmd.SetViewport(RHI::RHIViewport{0.f, 0.f, float(w), float(h)});
            cmd.SetScissor(RHI::RHIScissor{0, 0, w, h});

            for (const auto& item : drawItems) {
                if (!item.pipeline.IsValid()) continue;
                const auto* wt = regPtr->try_get<WorldTransformComponent>(item.entity);
                if (!wt) continue;
                const glm::mat4 world = wt->matrix * item.subLocalTransform;

                cmd.SetPipeline(item.pipeline);
                cmd.SetDescriptorSet(0, frameSet);
                item.material->Bind(&cmd);
                cmd.SetVertexBuffer(0, item.vertexBuffer);
                cmd.SetIndexBuffer(item.indexBuffer);
                if (item.pushConstantSize > 0)
                    cmd.SetPushConstants(&world, item.pushConstantSize,
                                         RHI::RHIShaderStage::Vertex);
                cmd.DrawIndexed(item.indexCount, 1, item.firstIndex,
                                item.vertexOffset, 0);
            }
            cmd.EndRenderPass();
        });
}

// ── DeferredLightingFeature ───────────────────────────────────────────────────

void SceneRenderer::DeferredLightingFeature::OnInit(const FeatureInitContext& ctx)
{
    ctx.matMgr->RegisterTypeFromShaders(
        {"DeferredLighting", "fullscreen_tri", "deferred_lighting",
         RHI::RHICullMode::None, RHI::RHIBlendMode::AlphaBlend, false, false, true}, ctx);
    m_type = ctx.matMgr->GetType("DeferredLighting");
    if (!m_type) { SA_LOG_WARN("DeferredLightingFeature: shader load failed"); return; }
    m_gbDescSet = ctx.device->AllocateDescriptorSet(m_type->shader.GetMaterialLayout());
}

void SceneRenderer::DeferredLightingFeature::AddPasses(SceneRenderer& renderer,
                                                        const FrameContext& ctx,
                                                        const RendererHandles& handles,
                                                        const entt::registry& /*reg*/,
                                                        uint32_t w, uint32_t h)
{
    if (!m_type || !m_gbDescSet.IsValid()) return;

    // Re-bind G-Buffer textures whenever the viewport (and thus textures) change.
    if (w != m_trackedW || h != m_trackedH) {
        ctx.BindTexture(m_gbDescSet, 0, handles.gbufferRT0);
        ctx.BindTexture(m_gbDescSet, 1, handles.gbufferRT1);
        ctx.BindTexture(m_gbDescSet, 2, handles.gbufferRT2);
        ctx.BindTexture(m_gbDescSet, 3, handles.depth);
        ctx.BindTexture(m_gbDescSet, 4, handles.shadowMap);
        m_trackedW = w;
        m_trackedH = h;
    }

    AttachmentKey hdrKey{};
    hdrKey.colorCount      = 1;
    hdrKey.colorFormats[0] = RHI::RHIFormat::RGBA16F;
    hdrKey.depthFormat     = RHI::RHIFormat::Undefined;

    // AlphaBlend: alpha=0 from background pixels preserves the skybox written first.
    const RHI::RHIPipelineHandle pipeline = m_type->GetOrCreatePipeline(ctx.device, hdrKey);

    const RHI::RHIDescSetHandle frameSet  = ctx.frameSet;
    const RHI::RHIDescSetHandle gbDescSet = m_gbDescSet;
    const RGTextureHandle rgRT0      = handles.gbufferRT0;
    const RGTextureHandle rgRT1      = handles.gbufferRT1;
    const RGTextureHandle rgRT2      = handles.gbufferRT2;
    const RGTextureHandle rgDepth    = handles.depth;
    const RGTextureHandle rgHdr      = handles.hdr;
    const RGTextureHandle rgShadowMap = handles.shadowMap;

    ctx.rg->AddPass("DeferredLighting",
        [rgRT0, rgRT1, rgRT2, rgDepth, rgHdr, rgShadowMap](RGPassBuilder& b) {
            b.Read(rgRT0);
            b.Read(rgRT1);
            b.Read(rgRT2);
            b.Read(rgDepth);
            b.Read(rgShadowMap);  // triggers layout transition DEPTH_ATTACHMENT → SHADER_READ_ONLY
            b.Write(rgHdr);       // Skybox → DeferredLighting edge now comes from RG Compile
        },
        [pipeline, frameSet, gbDescSet, rgHdr, w, h]
        (RHI::IRHICommandList& cmd, const RGResources& res)
        {
            RHI::RHIRenderPassDesc rpDesc{};
            rpDesc.colorAttachmentCount = 1;
            rpDesc.colorAttachments[0].texture     = res.Get(rgHdr);
            rpDesc.colorAttachments[0].clearOnLoad = false;  // preserve skybox
            rpDesc.width  = w;
            rpDesc.height = h;

            cmd.BeginRenderPass(rpDesc);
            cmd.SetViewport(RHI::RHIViewport{0.f, 0.f, float(w), float(h)});
            cmd.SetScissor(RHI::RHIScissor{0, 0, w, h});
            cmd.SetPipeline(pipeline);
            cmd.SetDescriptorSet(0, frameSet);
            cmd.SetDescriptorSet(1, gbDescSet);
            cmd.Draw(3, 1, 0, 0);
            cmd.EndRenderPass();
        });
}

// ── TonemapFeature ────────────────────────────────────────────────────────────

void SceneRenderer::TonemapFeature::OnInit(const FeatureInitContext& ctx)
{
    ctx.matMgr->RegisterTypeFromShaders(
        {"Tonemap", "fullscreen_tri", "postfx_tonemap",
         RHI::RHICullMode::None, RHI::RHIBlendMode::Opaque, false, false, true}, ctx);
    m_type = ctx.matMgr->GetType("Tonemap");
    if (!m_type) { SA_LOG_WARN("TonemapFeature: shader load failed"); return; }
    m_hdrDescSet = ctx.device->AllocateDescriptorSet(m_type->shader.GetMaterialLayout());
}

void SceneRenderer::TonemapFeature::AddPasses(SceneRenderer& /*renderer*/,
                                               const FrameContext& ctx,
                                               const RendererHandles& handles,
                                               const entt::registry& /*reg*/,
                                               uint32_t w, uint32_t h)
{
    if (!m_type || !m_hdrDescSet.IsValid()) return;

    if (w != m_trackedW || h != m_trackedH) {
        ctx.BindTexture(m_hdrDescSet, 0, handles.hdr);
        m_trackedW = w;
        m_trackedH = h;
    }

    AttachmentKey swapKey{};
    swapKey.colorCount      = 1;
    swapKey.colorFormats[0] = ctx.device->GetSwapchainFormat();
    swapKey.depthFormat     = RHI::RHIFormat::Undefined;

    const RHI::RHIPipelineHandle pipeline = m_type->GetOrCreatePipeline(ctx.device, swapKey);

    const RHI::RHIDescSetHandle frameSet   = ctx.frameSet;
    const RHI::RHIDescSetHandle hdrDescSet = m_hdrDescSet;
    const RGTextureHandle rgHdr       = handles.hdr;
    const RGTextureHandle rgSwapchain = handles.swapchain;

    struct TonemapPC { float exposure; float gamma; float _pad0; float _pad1; };
    constexpr TonemapPC pc{1.0f, 2.2f, 0.f, 0.f};

    ctx.rg->AddPass("Tonemap",
        [rgHdr, rgSwapchain](RGPassBuilder& b) {
            b.Read(rgHdr);
            b.Write(rgSwapchain);
        },
        [pipeline, frameSet, hdrDescSet, pc, rgSwapchain, w, h]
        (RHI::IRHICommandList& cmd, const RGResources& res)
        {
            RHI::RHIRenderPassDesc rpDesc{};
            rpDesc.colorAttachmentCount = 1;
            rpDesc.colorAttachments[0].texture     = res.Get(rgSwapchain);
            rpDesc.colorAttachments[0].clearOnLoad = true;
            rpDesc.width  = w;
            rpDesc.height = h;

            cmd.BeginRenderPass(rpDesc);
            cmd.SetViewport(RHI::RHIViewport{0.f, 0.f, float(w), float(h)});
            cmd.SetScissor(RHI::RHIScissor{0, 0, w, h});
            cmd.SetPipeline(pipeline);
            cmd.SetDescriptorSet(0, frameSet);
            cmd.SetDescriptorSet(1, hdrDescSet);
            cmd.SetPushConstants(&pc, sizeof(pc), RHI::RHIShaderStage::Fragment);
            cmd.Draw(3, 1, 0, 0);
            cmd.EndRenderPass();
        });
}

// ── BloomFeature ──────────────────────────────────────────────────────────────

void SceneRenderer::BloomFeature::OnInit(const FeatureInitContext& ctx)
{
    ctx.matMgr->RegisterTypeFromShaders(
        {"BloomThreshold",  "fullscreen_tri", "bloom_threshold",
         RHI::RHICullMode::None, RHI::RHIBlendMode::Opaque,    false, false, true}, ctx);
    ctx.matMgr->RegisterTypeFromShaders(
        {"BloomDownsample", "fullscreen_tri", "bloom_downsample",
         RHI::RHICullMode::None, RHI::RHIBlendMode::Opaque,    false, false, true}, ctx);
    ctx.matMgr->RegisterTypeFromShaders(
        {"BloomUpsample",   "fullscreen_tri", "bloom_upsample",
         RHI::RHICullMode::None, RHI::RHIBlendMode::Additive,  false, false, true}, ctx);
    ctx.matMgr->RegisterTypeFromShaders(
        {"BloomComposite",  "fullscreen_tri", "bloom_composite",
         RHI::RHICullMode::None, RHI::RHIBlendMode::Additive,  false, false, true}, ctx);

    m_thresholdType  = ctx.matMgr->GetType("BloomThreshold");
    m_downsampleType = ctx.matMgr->GetType("BloomDownsample");
    m_upsampleType   = ctx.matMgr->GetType("BloomUpsample");
    m_compositeType  = ctx.matMgr->GetType("BloomComposite");
    if (!m_thresholdType || !m_downsampleType || !m_upsampleType || !m_compositeType) {
        SA_LOG_WARN("BloomFeature: one or more shaders failed to load"); return;
    }

    // All bloom types share the same set=1 layout (single sampler2D at binding=0).
    // Allocate 1 + (mipCount-1) + (mipCount-1) + 1 descriptor sets.
    const auto layout = m_thresholdType->shader.GetMaterialLayout();
    m_thresholdDescSet = ctx.device->AllocateDescriptorSet(layout);
    for (int i = 0; i < m_mipCount - 1; ++i) {
        m_downsampleDescSet[i] = ctx.device->AllocateDescriptorSet(layout);
        m_upsampleDescSet[i]   = ctx.device->AllocateDescriptorSet(layout);
    }
    m_compositeDescSet = ctx.device->AllocateDescriptorSet(layout);
}

void SceneRenderer::BloomFeature::AddPasses(SceneRenderer& renderer,
                                             const FrameContext& ctx,
                                             const RendererHandles& handles,
                                             const entt::registry& /*reg*/,
                                             uint32_t w, uint32_t h)
{
    if (!m_thresholdType || !m_downsampleType || !m_upsampleType || !m_compositeType) return;
    if (!m_thresholdDescSet.IsValid()) return;

    // Re-bind input textures on viewport resize.
    if (w != m_trackedW || h != m_trackedH) {
        ctx.BindTexture(m_thresholdDescSet, 0, handles.hdr);
        // downsampleDescSet[i] reads mip[i]
        for (int i = 0; i < m_mipCount - 1; ++i)
            ctx.BindTexture(m_downsampleDescSet[i], 0, handles.bloomMip[i]);
        // upsampleDescSet[i] reads mip[m_mipCount-1-i]  (bottom → top order)
        for (int i = 0; i < m_mipCount - 1; ++i)
            ctx.BindTexture(m_upsampleDescSet[i], 0, handles.bloomMip[m_mipCount - 1 - i]);
        ctx.BindTexture(m_compositeDescSet, 0, handles.bloomMip[0]);
        m_trackedW = w;
        m_trackedH = h;
    }

    AttachmentKey bloomKey{};
    bloomKey.colorCount      = 1;
    bloomKey.colorFormats[0] = RHI::RHIFormat::RGBA16F;
    bloomKey.depthFormat     = RHI::RHIFormat::Undefined;

    const auto pipeThreshold  = m_thresholdType ->GetOrCreatePipeline(ctx.device, bloomKey);
    const auto pipeDownsample = m_downsampleType->GetOrCreatePipeline(ctx.device, bloomKey);
    const auto pipeUpsample   = m_upsampleType  ->GetOrCreatePipeline(ctx.device, bloomKey);
    const auto pipeComposite  = m_compositeType ->GetOrCreatePipeline(ctx.device, bloomKey);

    // Copy handles into plain arrays for lambda capture (no pointer-to-member).
    const RHI::RHIDescSetHandle frameSet         = ctx.frameSet;
    const RHI::RHIDescSetHandle threshDescSet    = m_thresholdDescSet;
    const RHI::RHIDescSetHandle compositeDescSet = m_compositeDescSet;
    RGTextureHandle           rgMip[kMaxBloomMips];
    RHI::RHIDescSetHandle     dsSet[kMaxBloomMips - 1];
    RHI::RHIDescSetHandle     usSet[kMaxBloomMips - 1];
    uint32_t mipW[kMaxBloomMips], mipH[kMaxBloomMips];
    for (int i = 0; i < m_mipCount; ++i) {
        rgMip[i] = handles.bloomMip[i];
        mipW[i]  = renderer.m_bloomMipW[i];
        mipH[i]  = renderer.m_bloomMipH[i];
    }
    for (int i = 0; i < m_mipCount - 1; ++i) {
        dsSet[i] = m_downsampleDescSet[i];
        usSet[i] = m_upsampleDescSet[i];
    }
    const RGTextureHandle rgHdr = handles.hdr;

    // ── Threshold: HDR → mip[0] ───────────────────────────────────────────────
    struct ThresholdPC { float threshold; float knee; float p0; float p1; };
    constexpr ThresholdPC threshPC{1.0f, 0.1f, 0.f, 0.f};
    {
        const RGTextureHandle dst = rgMip[0];
        const uint32_t tw = mipW[0], th = mipH[0];
        ctx.rg->AddPass("BloomThreshold",
            [rgHdr, dst](RGPassBuilder& b) { b.Read(rgHdr); b.Write(dst); },
            [pipeThreshold, frameSet, threshDescSet, threshPC, dst, tw, th]
            (RHI::IRHICommandList& cmd, const RGResources& res) {
                RHI::RHIRenderPassDesc rp{};
                rp.colorAttachmentCount            = 1;
                rp.colorAttachments[0].texture     = res.Get(dst);
                rp.colorAttachments[0].clearOnLoad = true;
                rp.width = tw; rp.height = th;
                cmd.BeginRenderPass(rp);
                cmd.SetViewport(RHI::RHIViewport{0.f, 0.f, float(tw), float(th)});
                cmd.SetScissor(RHI::RHIScissor{0, 0, tw, th});
                cmd.SetPipeline(pipeThreshold);
                cmd.SetDescriptorSet(0, frameSet);
                cmd.SetDescriptorSet(1, threshDescSet);
                cmd.SetPushConstants(&threshPC, sizeof(threshPC), RHI::RHIShaderStage::Fragment);
                cmd.Draw(3, 1, 0, 0);
                cmd.EndRenderPass();
            });
    }

    // ── Downsample: mip[i] → mip[i+1] ────────────────────────────────────────
    for (int i = 0; i < m_mipCount - 1; ++i) {
        const RGTextureHandle src = rgMip[i];
        const RGTextureHandle dst = rgMip[i + 1];
        const uint32_t dw = mipW[i + 1], dh = mipH[i + 1];
        const RHI::RHIDescSetHandle ds = dsSet[i];
        ctx.rg->AddPass("BloomDown" + std::to_string(i),
            [src, dst](RGPassBuilder& b) { b.Read(src); b.Write(dst); },
            [pipeDownsample, frameSet, ds, dst, dw, dh]
            (RHI::IRHICommandList& cmd, const RGResources& res) {
                RHI::RHIRenderPassDesc rp{};
                rp.colorAttachmentCount            = 1;
                rp.colorAttachments[0].texture     = res.Get(dst);
                rp.colorAttachments[0].clearOnLoad = true;
                rp.width = dw; rp.height = dh;
                cmd.BeginRenderPass(rp);
                cmd.SetViewport(RHI::RHIViewport{0.f, 0.f, float(dw), float(dh)});
                cmd.SetScissor(RHI::RHIScissor{0, 0, dw, dh});
                cmd.SetPipeline(pipeDownsample);
                cmd.SetDescriptorSet(0, frameSet);
                cmd.SetDescriptorSet(1, ds);
                cmd.Draw(3, 1, 0, 0);
                cmd.EndRenderPass();
            });
    }

    // ── Upsample: mip[i+1] → mip[i], additive accumulation ──────────────────
    // Iterates i = m_mipCount-2 downto 0 (fine → coarse).
    // usSet[passIdx] was bound to mip[m_mipCount-1-passIdx], matching src each time.
    // Per-layer radius: deepest mip gets widest spread, shallowest stays tight.
    struct UpsamplePC { float radius; float p0, p1, p2; };
    // Enough entries for kMaxBloomMips-1 passes; only [0..m_mipCount-2] are used.
    constexpr float kUpsampleRadii[kMaxBloomMips - 1] = {1.00f, 0.85f, 0.70f, 0.60f, 0.55f, 0.50f, 0.45f};
    for (int i = m_mipCount - 2; i >= 0; --i) {
        const int passIdx           = (m_mipCount - 2) - i;
        const RGTextureHandle src   = rgMip[i + 1];
        const RGTextureHandle dst   = rgMip[i];
        const uint32_t dw = mipW[i], dh = mipH[i];
        const RHI::RHIDescSetHandle us = usSet[passIdx];
        const UpsamplePC upPC{kUpsampleRadii[passIdx], 0.f, 0.f, 0.f};
        ctx.rg->AddPass("BloomUp" + std::to_string(i),
            [src, dst](RGPassBuilder& b) { b.Read(src); b.Write(dst); },
            [pipeUpsample, frameSet, us, upPC, dst, dw, dh]
            (RHI::IRHICommandList& cmd, const RGResources& res) {
                RHI::RHIRenderPassDesc rp{};
                rp.colorAttachmentCount            = 1;
                rp.colorAttachments[0].texture     = res.Get(dst);
                rp.colorAttachments[0].clearOnLoad = false;  // additive: preserve coarser mip
                rp.width = dw; rp.height = dh;
                cmd.BeginRenderPass(rp);
                cmd.SetViewport(RHI::RHIViewport{0.f, 0.f, float(dw), float(dh)});
                cmd.SetScissor(RHI::RHIScissor{0, 0, dw, dh});
                cmd.SetPipeline(pipeUpsample);
                cmd.SetDescriptorSet(0, frameSet);
                cmd.SetDescriptorSet(1, us);
                cmd.SetPushConstants(&upPC, sizeof(upPC), RHI::RHIShaderStage::Fragment);
                cmd.Draw(3, 1, 0, 0);
                cmd.EndRenderPass();
            });
    }

    // ── Composite: mip[0] → HDR (additive, preserves lighting) ──────────────
    struct CompositePC { float strength; float p0; float p1; float p2; };
    constexpr CompositePC compPC{0.4f, 0.f, 0.f, 0.f};
    {
        const RGTextureHandle src = rgMip[0];
        ctx.rg->AddPass("BloomComposite",
            [src, rgHdr](RGPassBuilder& b) { b.Read(src); b.Write(rgHdr); },
            [pipeComposite, frameSet, compositeDescSet, compPC, rgHdr, w, h]
            (RHI::IRHICommandList& cmd, const RGResources& res) {
                RHI::RHIRenderPassDesc rp{};
                rp.colorAttachmentCount            = 1;
                rp.colorAttachments[0].texture     = res.Get(rgHdr);
                rp.colorAttachments[0].clearOnLoad = false;
                rp.width = w; rp.height = h;
                cmd.BeginRenderPass(rp);
                cmd.SetViewport(RHI::RHIViewport{0.f, 0.f, float(w), float(h)});
                cmd.SetScissor(RHI::RHIScissor{0, 0, w, h});
                cmd.SetPipeline(pipeComposite);
                cmd.SetDescriptorSet(0, frameSet);
                cmd.SetDescriptorSet(1, compositeDescSet);
                cmd.SetPushConstants(&compPC, sizeof(compPC), RHI::RHIShaderStage::Fragment);
                cmd.Draw(3, 1, 0, 0);
                cmd.EndRenderPass();
            });
    }
}

} // namespace StellarAlia
