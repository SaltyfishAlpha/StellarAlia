#include "function/renderer/SceneRenderer.hpp"

#include "core/logs/Log.hpp"
#include "function/material/AttachmentKey.hpp"
#include "function/material/MaterialType.hpp"
#include "function/scene/Components.hpp"
#include "platform/rhi/ShaderReflectionIO.hpp"
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
    if (!m_iblBake.Init(desc.device, desc.shaderDir)) {
        SA_LOG_WARN("SceneRenderer: GpuIblBake init failed — IBL bake unavailable");
    } else {
        // Pre-bake the BRDF LUT immediately (no HDR needed).
        // Cached so ApplyWorldSettings can use it even before a Skybox is ever loaded.
        m_cachedBrdfLut = m_iblBake.BakeBrdfLut(desc.device);
    }

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

    // ── Selection mask + dilateH intermediate (1×1 placeholders, resized on first RenderFrame) ──
    {
        auto makeR8 = [&](const char* name) {
            RHI::RHITextureDesc d{};
            d.width     = 1;
            d.height    = 1;
            d.format    = RHI::RHIFormat::R8_UNORM;
            d.usage     = RHI::RHITextureUsage::RenderTarget
                        | RHI::RHITextureUsage::Sampled;
            d.debugName = name;
            return desc.device->CreateTexture(d);
        };
        m_selectionMask  = makeR8("SelectionMask");
        m_dilateH        = makeR8("DilateH");
        m_selectionMaskW = 1;
        m_selectionMaskH = 1;
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
    //   [Shadow?, Skybox, GBuffer, DeferredLighting, SelectionMask, Bloom?, Tonemap?,
    //    ...user features, SelectionOutline, DebugOverlay]
    if (m_config.builtinTonemap) {
        auto tf = std::make_unique<TonemapFeature>();
        m_tonemapFeature = tf.get();
        m_features.insert(m_features.begin(), std::move(tf));
    }
    if (m_config.bloomEnabled)
        m_features.insert(m_features.begin(), std::make_unique<BloomFeature>(m_bloomMipCount));
    m_features.insert(m_features.begin(), std::make_unique<DeferredLightingFeature>());
    // SelectionMask runs immediately after DeferredLighting (depth is populated,
    // already transitioned back to depth-attachment by this WriteDepth declaration).
    m_features.insert(m_features.begin() + 1, std::make_unique<SelectionMaskFeature>(this));
    m_features.insert(m_features.begin(), std::make_unique<GBufferFeature>(this));
    {
        auto sf = std::make_unique<SkyboxFeature>();
        m_skyboxFeature = sf.get();
        m_features.insert(m_features.begin(), std::move(sf));
    }
    if (m_config.shadowEnabled)
        m_features.insert(m_features.begin(), std::make_unique<ShadowFeature>());

    // ── Selection outline + infinite grid + debug overlay — always last ─────────
    m_features.push_back(std::make_unique<SelectionOutlineFeature>());
    m_features.push_back(std::make_unique<InfiniteGridFeature>());
    m_features.push_back(std::make_unique<DebugOverlayFeature>());

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

    if (m_selectionMask.IsValid()) m_device->DestroyTexture(m_selectionMask);
    if (m_dilateH.IsValid())       m_device->DestroyTexture(m_dilateH);

    if (m_iblBake.IsInitialized())
        m_iblBake.Shutdown(m_device);
    if (m_ltcBake.IsUploaded())
        m_ltcBake.Shutdown(m_device);
    if (m_depthTex.IsValid())
        m_device->DestroyTexture(m_depthTex);
    if (m_solidAmbientCube.IsValid())
        m_device->DestroyTexture(m_solidAmbientCube);

    m_frameUniforms.Shutdown();
    m_ready = false;
}

// ── SetIBL ────────────────────────────────────────────────────────────────────

bool SceneRenderer::SetIBL(WorldSettings& ws)
{
    // Reset IBL state unconditionally — cleared first, overwritten on success.
    // This ensures stale baked textures don't persist when switching to SolidColor.
    for (int i = 0; i < 9; ++i) m_shCoeffs[i] = {};
    m_frameUniforms.SetIBLTextures({}, {}, {});

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
            m_cachedBrdfLut = blt;
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
    m_cachedBrdfLut = r.brdfLut;

    // Assign stable asset IDs so the baked textures can be cached on disk and
    // the WorldSettings reflects "baked" status (hasBaked check in the panel).
    if (!ws.brdfLut.IsValid())        ws.brdfLut        = AssetID::Generate();
    if (!ws.prefilteredEnv.IsValid()) ws.prefilteredEnv = AssetID::Generate();
    if (!ws.skyboxCubemap.IsValid())  ws.skyboxCubemap  = AssetID::Generate();
    if (!ws.sh9.IsValid())            ws.sh9            = AssetID::Generate();

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

// ── ApplyWorldSettings ────────────────────────────────────────────────────────

void SceneRenderer::ApplyWorldSettings(WorldSettings& ws, bool updateIBL)
{
    // Update skybox background mode + color immediately (no GPU work needed).
    if (m_skyboxFeature) {
        m_skyboxFeature->m_backgroundMode  = ws.backgroundMode;
        m_skyboxFeature->m_backgroundColor = ws.backgroundColor;
    }

    // IBL — solid-color mode encodes backgroundColor as constant ambient (SH L0);
    // Skybox mode loads/bakes from HDR.  SolidColor always overrides IBL state
    // even when ws.skyboxHdr is still set from a prior Skybox session.
    if (updateIBL) {
        if (ws.backgroundMode == WorldSettings::BackgroundMode::SolidColor) {
            // SH diffuse — encode backgroundColor as constant ambient (L0 term only).
            // EvaluateSHIrradiance: irrSH[0] * 0.282095 = desired irradiance.
            for (int i = 0; i < 9; ++i) m_shCoeffs[i] = {};
            m_shCoeffs[0] = glm::vec4(ws.backgroundColor / 0.282095f, 0.f);
            // Specular — write backgroundColor into a 1×1 cubemap so metallic
            // surfaces reflect the ambient color (matches Unity "Ambient Color" mode).
            // If no BRDF LUT is cached yet, specular falls back to the black placeholder.
            UpdateSolidAmbientCube(ws.backgroundColor);
            m_frameUniforms.SetIBLTextures(m_cachedBrdfLut,
                                            m_solidAmbientCube, m_solidAmbientCube);
        } else {
            SetIBL(ws);
        }
    }

    // Tonemap — update params or replace the feature if mode changed.
    if (!m_tonemapFeature) return;

    const FeatureInitContext ctx{m_device, m_matMgr, m_resMgr,
                                  m_frameUniforms.GetLayout(), m_shaderDir};

    if (ws.tonemapMode == WorldSettings::TonemapMode::Builtin) {
        if (auto* tf = dynamic_cast<TonemapFeature*>(m_tonemapFeature)) {
            tf->m_exposure = ws.exposure;
            tf->m_gamma    = ws.gamma;
        } else {
            // Replace LUT → Builtin
            m_device->WaitIdle();
            auto newFeature = std::make_unique<TonemapFeature>();
            newFeature->m_exposure = ws.exposure;
            newFeature->m_gamma    = ws.gamma;
            ReplaceTonemapFeature(std::move(newFeature), ctx);
        }
    } else {
        // LUT tonemap — requires a valid, loaded texture.
        // If no LUT is set, fall back to the builtin ACES pipeline instead.
        RHI::RHITextureHandle lutTex;
        if (ws.tonemapLut.IsValid())
            lutTex = m_resMgr->LoadTexture(ws.tonemapLut);

        if (!lutTex.IsValid()) {
            SA_LOG_WARN("SceneRenderer: LUT mode requested but no valid LUT texture — falling back to builtin tonemap");
            if (!dynamic_cast<TonemapFeature*>(m_tonemapFeature)) {
                m_device->WaitIdle();
                auto newFeature = std::make_unique<TonemapFeature>();
                newFeature->m_exposure = ws.exposure;
                newFeature->m_gamma    = ws.gamma;
                ReplaceTonemapFeature(std::move(newFeature), ctx);
            } else if (auto* tf = dynamic_cast<TonemapFeature*>(m_tonemapFeature)) {
                tf->m_exposure = ws.exposure;
                tf->m_gamma    = ws.gamma;
            }
            return;
        }

        if (auto* lf = dynamic_cast<LutTonemapFeature*>(m_tonemapFeature)) {
            lf->m_exposure    = ws.exposure;
            lf->m_lutStrength = ws.lutStrength;
            lf->SetLutTexture(m_device, lutTex);
        } else {
            // Replace Builtin → LUT
            m_device->WaitIdle();
            auto newFeature = std::make_unique<LutTonemapFeature>();
            newFeature->m_exposure    = ws.exposure;
            newFeature->m_lutStrength = ws.lutStrength;
            ReplaceTonemapFeature(std::move(newFeature), ctx);
            static_cast<LutTonemapFeature*>(m_tonemapFeature)->SetLutTexture(m_device, lutTex);
        }
    }
}

// ── RebakeIBL ─────────────────────────────────────────────────────────────────

void SceneRenderer::RebakeIBL(WorldSettings& ws)
{
    // Delete stale cached files so the cook cache doesn't accumulate orphans.
    auto tryDelete = [&](const AssetID& id, const char* ext) {
        if (!id.IsValid() || m_cookCacheDir.empty()) return;
        std::error_code ec;
        std::filesystem::remove(m_cookCacheDir + "/" + id.ToString() + ext, ec);
    };
    tryDelete(ws.brdfLut,        ".satex");
    tryDelete(ws.prefilteredEnv, ".satex");
    tryDelete(ws.skyboxCubemap,  ".satex");
    tryDelete(ws.sh9,            ".sash9");

    ws.sh9 = ws.prefilteredEnv = ws.brdfLut = ws.skyboxCubemap = AssetID{};
    ApplyWorldSettings(ws);
}

// Creates (or recreates) a 1×1 RGBA32F cubemap filled with `color`.
// Called by ApplyWorldSettings in SolidColor mode for the specular ambient fallback.
void SceneRenderer::UpdateSolidAmbientCube(glm::vec3 color)
{
    if (m_solidAmbientCube.IsValid() && color == m_solidAmbientColor) return;

    if (m_solidAmbientCube.IsValid()) {
        m_device->WaitIdle();
        m_device->DestroyTexture(m_solidAmbientCube);
    }

    RHI::RHITextureDesc d{};
    d.width = d.height = 1;
    d.cubemap   = true;
    d.format    = RHI::RHIFormat::RGBA32F;
    d.usage     = RHI::RHITextureUsage::Sampled;
    d.debugName = "SolidAmbientCube";
    m_solidAmbientCube = m_device->CreateTexture(d);

    const float px[4] = { color.r, color.g, color.b, 1.f };
    float buf[6 * 4];
    for (int f = 0; f < 6; ++f)
        std::copy(px, px + 4, buf + f * 4);
    m_device->UploadTextureData(m_solidAmbientCube, buf, sizeof(buf));

    m_solidAmbientColor = color;
}

// Finds the current m_tonemapFeature slot in m_features, shuts it down, replaces it,
// and updates m_tonemapFeature.  Called only from ApplyWorldSettings.
void SceneRenderer::ReplaceTonemapFeature(std::unique_ptr<RenderFeature> newFeature,
                                           const FeatureInitContext& ctx)
{
    for (auto& slot : m_features) {
        if (slot.get() == m_tonemapFeature) {
            slot->OnShutdown(m_device);
            newFeature->OnInit(ctx);
            m_tonemapFeature = newFeature.get();
            slot = std::move(newFeature);
            return;
        }
    }
    SA_LOG_WARN("SceneRenderer::ReplaceTonemapFeature: slot not found");
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

        const auto* matOverride  = reg.try_get<MaterialOverrideComponent>(e);
        const auto* meshRenderer = reg.try_get<MeshRendererComponent>(e);

        for (size_t si = 0; si < gpuMesh->subMeshes.size(); ++si) {
            const auto& sub = gpuMesh->subMeshes[si];

            MaterialInstance* base = m_defaultMaterial.get();

            if (meshRenderer && si < meshRenderer->materialSlots.size() &&
                meshRenderer->materialSlots[si].IsValid()) {
                MaterialInstance* loaded = m_matMgr->LoadMaterial(
                    meshRenderer->materialSlots[si], *m_resMgr);
                if (loaded) base = loaded;
            } else if (sub.defaultMaterialID.IsValid()) {
                MaterialInstance* loaded = m_matMgr->LoadMaterial(
                    sub.defaultMaterialID, *m_resMgr);
                if (loaded) base = loaded;
            }

            // materialAsset override replaces the resolved base for all sub-meshes.
            if (matOverride && matOverride->materialAsset.IsValid()) {
                MaterialInstance* loaded = m_matMgr->LoadMaterial(
                    matOverride->materialAsset, *m_resMgr);
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

            if (matOverride && (!matOverride->scalars.empty() || !matOverride->textures.empty())) {
                auto clone = m_matMgr->CloneInstance(base);
                if (!clone) { item.material = base; }
                else {
                    for (const auto& [name, val] : matOverride->scalars)
                        std::visit([&](const auto& v){ clone->SetParam(name, v); }, val);
                    for (const auto& [name, texID] : matOverride->textures)
                        if (texID.IsValid())
                            clone->SetTexture(name, m_resMgr->LoadTexture(texID));
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

    // ── Skinned meshes ────────────────────────────────────────────────────────
    // SkinnedMeshComponent::dynVertexBuffer is updated each frame by AnimationSystem.
    // We build draw items once (like static meshes) — same buffer handle, new contents.
    scene.View<SkinnedMeshComponent, WorldTransformComponent>().each(
        [&](entt::entity e,
            const SkinnedMeshComponent&    meshComp,
            const WorldTransformComponent& /*wt*/)
    {
        if (!meshComp.ready || !meshComp.dynVertexBuffer.IsValid()) return;

        const auto* matOverride  = reg.try_get<MaterialOverrideComponent>(e);
        const auto* meshRenderer = reg.try_get<MeshRendererComponent>(e);

        for (size_t si = 0; si < meshComp.subMeshes.size(); ++si) {
            const auto& sub = meshComp.subMeshes[si];

            MaterialInstance* base = m_defaultMaterial.get();

            if (meshRenderer && si < meshRenderer->materialSlots.size() &&
                meshRenderer->materialSlots[si].IsValid()) {
                MaterialInstance* loaded = m_matMgr->LoadMaterial(
                    meshRenderer->materialSlots[si], *m_resMgr);
                if (loaded) base = loaded;
            } else if (sub.materialAssetID.IsValid()) {
                MaterialInstance* loaded = m_matMgr->LoadMaterial(
                    sub.materialAssetID, *m_resMgr);
                if (loaded) base = loaded;
            }

            // materialAsset override replaces the resolved base for all sub-meshes.
            if (matOverride && matOverride->materialAsset.IsValid()) {
                MaterialInstance* loaded = m_matMgr->LoadMaterial(
                    matOverride->materialAsset, *m_resMgr);
                if (loaded) base = loaded;
            }

            DrawItem item{};
            item.entity            = e;
            item.subLocalTransform = glm::mat4(1.f);  // skeleton drives transforms
            item.vertexBuffer      = meshComp.dynVertexBuffer;
            item.indexBuffer       = meshComp.indexBuffer;
            item.firstIndex        = sub.firstIndex;
            item.indexCount        = sub.indexCount;
            item.vertexOffset      = sub.vertexOffset;

            if (matOverride && (!matOverride->scalars.empty() || !matOverride->textures.empty())) {
                auto clone = m_matMgr->CloneInstance(base);
                if (!clone) { item.material = base; }
                else {
                    for (const auto& [name, val] : matOverride->scalars)
                        std::visit([&](const auto& v){ clone->SetParam(name, v); }, val);
                    for (const auto& [name, texID] : matOverride->textures)
                        if (texID.IsValid())
                            clone->SetTexture(name, m_resMgr->LoadTexture(texID));
                    item.material      = clone.get();
                    item.ownedMaterial = std::move(clone);
                }
            } else {
                item.material = base;
            }

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

// ── ExtractCamera ─────────────────────────────────────────────────────────────

CameraData SceneRenderer::ExtractCamera(const Scene& scene,
                                         uint32_t w, uint32_t h) {
    CameraData out{};
    // Identity defaults — rendered frame will be black but won't crash.
    out.view = glm::mat4(1.f);
    out.proj = glm::mat4(1.f);

    int bestPriority = INT_MIN;
    scene.View<CameraComponent, WorldTransformComponent>().each(
        [&](auto, const CameraComponent& cam, const WorldTransformComponent& wt) {
            if (cam.priority <= bestPriority) return;
            bestPriority = cam.priority;
            const float aspect = (h > 0)
                ? static_cast<float>(w) / static_cast<float>(h)
                : 1.f;
            out.view           = glm::inverse(wt.matrix);
            out.proj           = glm::perspective(cam.fovY, aspect, cam.nearPlane, cam.farPlane);
            out.proj[1][1]    *= -1.f;   // Vulkan Y-flip
            out.worldPosition  = glm::vec3(wt.matrix[3]);
        });

    return out;
}

// ── ApplyCameraToUniforms ─────────────────────────────────────────────────────

// Analytical inverse of a rigid-body (rotation + translation) matrix.
// For V = [R | t ; 0 | 1]:  V⁻¹ = [Rᵀ | −Rᵀt ; 0 | 1]
// The rotation part is orthonormal, so transpose == inverse — exact in float.
// This is far more numerically stable than glm::inverse() on the composed
// matrix (P·V), whose mixed perspective/rotation entries ill-condition the
// general 4×4 inversion and produce skybox jitter during off-axis rotation.
static glm::mat4 RigidBodyInverse(const glm::mat4& v) {
    const glm::mat3 Rt = glm::transpose(glm::mat3(v));
    glm::mat4 result(Rt);
    result[3] = glm::vec4(-(Rt * glm::vec3(v[3])), 1.f);
    return result;
}

void SceneRenderer::ApplyCameraToUniforms(const CameraData& cam, FrameUniforms& fu) {
    fu.view        = cam.view;
    fu.proj        = cam.proj;
    fu.viewProj    = cam.proj * cam.view;
    // (P·V)⁻¹ = V⁻¹·P⁻¹.  Compute V⁻¹ analytically (rigid body — exact),
    // then multiply by P⁻¹ (single perspective matrix — well conditioned).
    // Avoids inverting the ill-conditioned composed matrix directly.
    fu.invViewProj = RigidBodyInverse(cam.view) * glm::inverse(cam.proj);
    fu.cameraPos   = cam.worldPosition;
}

// ── AddFeature ────────────────────────────────────────────────────────────────

void SceneRenderer::AddFeature(std::unique_ptr<RenderFeature> feature) {
    if (m_ready)
        feature->OnInit({m_device, m_matMgr, m_resMgr, m_frameUniforms.GetLayout(), m_shaderDir});
    m_features.push_back(std::move(feature));
}

// ── RenderFrame ───────────────────────────────────────────────────────────────

// ── RenderFrame (scene-camera wrapper) ───────────────────────────────────────

void SceneRenderer::RenderFrame(Scene& scene, uint32_t w, uint32_t h) {
    RenderFrame(scene, ExtractCamera(scene, w, h), w, h);
}

// ── RenderFrame (explicit camera — canonical implementation) ──────────────────

void SceneRenderer::RenderFrame(Scene& scene, const CameraData& camera,
                                 uint32_t w, uint32_t h, UIPassFn uiPass)
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

        {
            auto recreateR8 = [&](RHI::RHITextureHandle& tex, const char* name) {
                if (tex.IsValid()) m_device->DestroyTexture(tex);
                RHI::RHITextureDesc d{};
                d.width     = w;
                d.height    = h;
                d.format    = RHI::RHIFormat::R8_UNORM;
                d.usage     = RHI::RHITextureUsage::RenderTarget
                            | RHI::RHITextureUsage::Sampled;
                d.debugName = name;
                tex = m_device->CreateTexture(d);
            };
            recreateR8(m_selectionMask, "SelectionMask");
            recreateR8(m_dilateH,       "DilateH");
            m_selectionMaskW = w;
            m_selectionMaskH = h;
        }
    }

    // ── Phase 1: collect frame data ───────────────────────────────────────────
    FrameUniforms fu{};
    fu.resolution = {static_cast<float>(w), static_cast<float>(h)};
    fu.time       = static_cast<float>(m_frameCount) / 60.f;
    std::copy(std::begin(m_shCoeffs), std::end(m_shCoeffs), std::begin(fu.irrSH));
    ApplyCameraToUniforms(camera, fu);
    m_currentViewProj = fu.viewProj;
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

    // Rebuild draw-list after BeginFrame so the fence-wait has already retired
    // any GPU work that held references to the previous draw-items.
    if (scene.IsAndClearMaterialDirty()) BuildDrawList(scene);

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
    m_rgSelectionMask = m_rg.ImportTexture("SelectionMask", m_selectionMask,
        RHI::RHIResourceState::Undefined, RHI::RHIResourceState::Undefined);
    m_rgDilateH = m_rg.ImportTexture("DilateH", m_dilateH,
        RHI::RHIResourceState::Undefined, RHI::RHIResourceState::Undefined);

    // ── Build RendererHandles (RG handles for all built-in render targets) ───────
    RendererHandles handles{};
    handles.hdr           = m_rgHdr;
    handles.swapchain     = m_rgSwapchain;
    handles.depth         = m_rgDepth;
    handles.gbufferRT0    = m_rgGbRT0;
    handles.gbufferRT1    = m_rgGbRT1;
    handles.gbufferRT2    = m_rgGbRT2;
    handles.shadowMap     = m_rgShadowMap;
    handles.selectionMask = m_rgSelectionMask;
    handles.dilateH       = m_rgDilateH;
    handles.bloomMipCount = m_bloomMipCount;
    for (int i = 0; i < m_bloomMipCount; ++i)
        handles.bloomMip[i] = m_rgBloomMip[i];

    // ── Build RHI lookup table for FrameContext::BindTexture ──────────────────
    // Indexed by RGTextureHandle.index (assigned sequentially by ImportTexture).
    // We build a flat array: index → RHITextureHandle.
    // RG assigns indices starting from 0; the table size = highest index + 1.
    // We pre-fill with the known mapping here; unknown indices resolve to {}.
    // 7 non-bloom + kMaxBloomMips bloom + SelectionMask + DilateH = 9 + kMaxBloomMips slots.
    const uint32_t tableSize = 9 + static_cast<uint32_t>(kMaxBloomMips);
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
    fillEntry(m_rgSelectionMask, m_selectionMask);
    fillEntry(m_rgDilateH,       m_dilateH);

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

    if (uiPass)
        uiPass(m_cmd);

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
         RHI::RHICullMode::Front, RHI::RHIBlendMode::Opaque, RHI::RHITopology::TriangleList, true, true, false}, ctx);
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
         RHI::RHICullMode::None, RHI::RHIBlendMode::Opaque, RHI::RHITopology::TriangleList, false, false, true}, ctx);
    m_type = ctx.matMgr->GetType("Skybox");
    if (!m_type) SA_LOG_WARN("SkyboxFeature: shader load failed — skybox disabled");
}

void SceneRenderer::SkyboxFeature::AddPasses(SceneRenderer& /*renderer*/,
                                              const FrameContext& ctx,
                                              const RendererHandles& handles,
                                              const entt::registry& /*reg*/,
                                              uint32_t w, uint32_t h)
{
    const RGTextureHandle rgHdr   = handles.hdr;
    const glm::vec3       bgColor = m_backgroundColor;

    if (m_backgroundMode == WorldSettings::BackgroundMode::SolidColor) {
        ctx.rg->AddPass("Skybox",
            [rgHdr](RGPassBuilder& b) { b.Write(rgHdr); },
            [rgHdr, bgColor, w, h]
            (RHI::IRHICommandList& cmd, const RGResources& res)
            {
                RHI::RHIRenderPassDesc rpDesc{};
                rpDesc.colorAttachmentCount = 1;
                rpDesc.colorAttachments[0].texture        = res.Get(rgHdr);
                rpDesc.colorAttachments[0].clearOnLoad    = true;
                rpDesc.colorAttachments[0].clearColor[0]  = bgColor.r;
                rpDesc.colorAttachments[0].clearColor[1]  = bgColor.g;
                rpDesc.colorAttachments[0].clearColor[2]  = bgColor.b;
                rpDesc.colorAttachments[0].clearColor[3]  = 1.f;
                rpDesc.width  = w;
                rpDesc.height = h;
                cmd.BeginRenderPass(rpDesc);
                cmd.EndRenderPass();
            });
        return;
    }

    // Skybox mode — draw fullscreen skybox into HDR buffer.
    if (!m_type) return;

    AttachmentKey key{};
    key.colorCount      = 1;
    key.colorFormats[0] = RHI::RHIFormat::RGBA16F;
    key.depthFormat     = RHI::RHIFormat::Undefined;

    const RHI::RHIPipelineHandle pipeline = m_type->GetOrCreatePipeline(ctx.device, key);
    const RHI::RHIDescSetHandle  descSet  = ctx.frameSet;

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

    // Auto-register project material types compiled from .saglsl files.
    // ShaderCookTool embeds shadingModel + vertShader into *.gbuffer.frag.refl (v5+).
    ctx.matMgr->RegisterTypesFromShaderDir(ctx.shaderDir, ctx);
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
         RHI::RHICullMode::None, RHI::RHIBlendMode::AlphaBlend, RHI::RHITopology::TriangleList, false, false, true}, ctx);
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
         RHI::RHICullMode::None, RHI::RHIBlendMode::Opaque, RHI::RHITopology::TriangleList, false, false, true}, ctx);
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
    const TonemapPC pc{m_exposure, m_gamma, 0.f, 0.f};

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

// ── LutTonemapFeature ─────────────────────────────────────────────────────────

void SceneRenderer::LutTonemapFeature::OnInit(const FeatureInitContext& ctx)
{
    ctx.matMgr->RegisterTypeFromShaders(
        {"LutTonemap", "fullscreen_tri", "postfx_lut_tonemap",
         RHI::RHICullMode::None, RHI::RHIBlendMode::Opaque, RHI::RHITopology::TriangleList, false, false, true}, ctx);
    m_type = ctx.matMgr->GetType("LutTonemap");
    if (!m_type) { SA_LOG_WARN("LutTonemapFeature: shader load failed"); return; }
    m_hdrLutDescSet = ctx.device->AllocateDescriptorSet(m_type->shader.GetMaterialLayout());
    m_trackedW = m_trackedH = 0;
    // binding=1 (t_LUT) is written by SetLutTexture, called by ApplyWorldSettings
    // immediately after feature construction — never rendered before that.
}

void SceneRenderer::LutTonemapFeature::OnShutdown(RHI::IRHIDevice* /*device*/)
{
    // Descriptor sets are pool-allocated; no explicit free needed.
    m_hdrLutDescSet = {};
    m_type = nullptr;
}

void SceneRenderer::LutTonemapFeature::SetLutTexture(RHI::IRHIDevice* device,
                                                      RHI::RHITextureHandle tex)
{
    if (m_hdrLutDescSet.IsValid() && tex.IsValid())
        device->WriteDescriptorTexture(m_hdrLutDescSet, 1, tex);
}

void SceneRenderer::LutTonemapFeature::AddPasses(SceneRenderer& /*renderer*/,
                                                  const FrameContext& ctx,
                                                  const RendererHandles& handles,
                                                  const entt::registry& /*reg*/,
                                                  uint32_t w, uint32_t h)
{
    if (!m_type || !m_hdrLutDescSet.IsValid()) return;

    if (w != m_trackedW || h != m_trackedH) {
        ctx.BindTexture(m_hdrLutDescSet, 0, handles.hdr);
        m_trackedW = w;
        m_trackedH = h;
    }

    AttachmentKey swapKey{};
    swapKey.colorCount      = 1;
    swapKey.colorFormats[0] = ctx.device->GetSwapchainFormat();
    swapKey.depthFormat     = RHI::RHIFormat::Undefined;

    const RHI::RHIPipelineHandle pipeline     = m_type->GetOrCreatePipeline(ctx.device, swapKey);
    const RHI::RHIDescSetHandle  frameSet     = ctx.frameSet;
    const RHI::RHIDescSetHandle  hdrLutDescSet = m_hdrLutDescSet;
    const RGTextureHandle        rgHdr         = handles.hdr;
    const RGTextureHandle        rgSwapchain   = handles.swapchain;

    struct LutTonemapPC { float exposure; float lutStrength; float _pad0; float _pad1; };
    const LutTonemapPC pc{m_exposure, m_lutStrength, 0.f, 0.f};

    ctx.rg->AddPass("Tonemap",
        [rgHdr, rgSwapchain](RGPassBuilder& b) {
            b.Read(rgHdr);
            b.Write(rgSwapchain);
        },
        [pipeline, frameSet, hdrLutDescSet, pc, rgSwapchain, w, h]
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
            cmd.SetDescriptorSet(1, hdrLutDescSet);
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
         RHI::RHICullMode::None, RHI::RHIBlendMode::Opaque,   RHI::RHITopology::TriangleList, false, false, true}, ctx);
    ctx.matMgr->RegisterTypeFromShaders(
        {"BloomDownsample", "fullscreen_tri", "bloom_downsample",
         RHI::RHICullMode::None, RHI::RHIBlendMode::Opaque,   RHI::RHITopology::TriangleList, false, false, true}, ctx);
    ctx.matMgr->RegisterTypeFromShaders(
        {"BloomUpsample",   "fullscreen_tri", "bloom_upsample",
         RHI::RHICullMode::None, RHI::RHIBlendMode::Additive, RHI::RHITopology::TriangleList, false, false, true}, ctx);
    ctx.matMgr->RegisterTypeFromShaders(
        {"BloomComposite",  "fullscreen_tri", "bloom_composite",
         RHI::RHICullMode::None, RHI::RHIBlendMode::Additive, RHI::RHITopology::TriangleList, false, false, true}, ctx);

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

// ── DebugOverlayFeature ───────────────────────────────────────────────────────

void SceneRenderer::DebugOverlayFeature::OnInit(const FeatureInitContext& ctx)
{
    if (!ctx.matMgr->RegisterTypeFromShaders(
            {"DebugLine", "debug_line", "debug_line",
             RHI::RHICullMode::None, RHI::RHIBlendMode::Opaque,
             RHI::RHITopology::LineList, true, false, true}, ctx)) {
        SA_LOG_WARN("DebugOverlayFeature: shader load failed");
        return;
    }
    ctx.matMgr->RegisterTypeFromShaders(
        {"DebugLineXray", "debug_line", "debug_line",
         RHI::RHICullMode::None, RHI::RHIBlendMode::Opaque,
         RHI::RHITopology::LineList, false, false, true}, ctx);

    m_type     = ctx.matMgr->GetType("DebugLine");
    m_xrayType = ctx.matMgr->GetType("DebugLineXray");
    if (!m_type) return;

    RHI::RHIBufferDesc bd{};
    bd.size       = DebugDraw::kMaxVertices * sizeof(DebugDraw::Vertex);
    bd.usage      = RHI::RHIBufferUsage::Storage;
    bd.cpuVisible = true;

    bd.debugName = "DebugLineSSBO";
    m_ssbo    = ctx.device->CreateBuffer(bd);
    m_descSet = ctx.device->AllocateDescriptorSet(m_type->shader.GetMaterialLayout());
    ctx.device->WriteDescriptorBuffer(m_descSet, 0, m_ssbo, 0, bd.size);

    if (m_xrayType) {
        bd.debugName  = "DebugLineXraySSBO";
        m_xraySsbo    = ctx.device->CreateBuffer(bd);
        m_xrayDescSet = ctx.device->AllocateDescriptorSet(m_xrayType->shader.GetMaterialLayout());
        ctx.device->WriteDescriptorBuffer(m_xrayDescSet, 0, m_xraySsbo, 0, bd.size);
    }
}

void SceneRenderer::DebugOverlayFeature::OnShutdown(RHI::IRHIDevice* device)
{
    if (m_ssbo.IsValid())     device->DestroyBuffer(m_ssbo);
    if (m_xraySsbo.IsValid()) device->DestroyBuffer(m_xraySsbo);
    m_ssbo        = {};
    m_xraySsbo    = {};
    m_descSet     = {};
    m_xrayDescSet = {};
    m_type        = nullptr;
    m_xrayType    = nullptr;
}

void SceneRenderer::DebugOverlayFeature::AddPasses(
    SceneRenderer& renderer, const FrameContext& ctx,
    const RendererHandles& handles, const entt::registry& /*reg*/,
    uint32_t w, uint32_t h)
{
    if (!m_type || !m_ssbo.IsValid() || !m_descSet.IsValid()) return;

    DebugDraw* dd = renderer.m_debugDraw;
    if (!dd) return;

    // Key for depth-tested pass (depth attachment present).
    AttachmentKey key{};
    key.colorCount      = 1;
    key.colorFormats[0] = ctx.device->GetSwapchainFormat();
    key.depthFormat     = RHI::RHIFormat::D32F;

    // Key for xray pass (no depth attachment).
    AttachmentKey xrayKey{};
    xrayKey.colorCount      = 1;
    xrayKey.colorFormats[0] = ctx.device->GetSwapchainFormat();
    xrayKey.depthFormat     = RHI::RHIFormat::Undefined;

    const RGTextureHandle rgSwap   = handles.swapchain;
    const RGTextureHandle rgDepth  = handles.depth;
    const glm::mat4       viewProj = renderer.m_currentViewProj;

    // ── Depth-tested lines ────────────────────────────────────────────────────
    const auto     verts     = dd->GetVertices();
    const uint32_t vertCount = static_cast<uint32_t>(verts.size());
    if (vertCount > 0) {
        ctx.device->UploadBufferData(m_ssbo, verts.data(),
                                     vertCount * sizeof(DebugDraw::Vertex));

        const RHI::RHIPipelineHandle pipeline = m_type->GetOrCreatePipeline(ctx.device, key);
        if (pipeline.IsValid()) {
            const RHI::RHIDescSetHandle frameSet = ctx.frameSet;
            const RHI::RHIDescSetHandle descSet  = m_descSet;
            ctx.rg->AddPass("DebugOverlay",
                [rgSwap, rgDepth](RGPassBuilder& b) {
                    b.Read(rgSwap); b.Write(rgSwap); b.WriteDepth(rgDepth);
                },
                [pipeline, frameSet, descSet, viewProj, vertCount, rgSwap, rgDepth, w, h]
                (RHI::IRHICommandList& cmd, const RGResources& res)
                {
                    RHI::RHIRenderPassDesc rpDesc{};
                    rpDesc.colorAttachmentCount            = 1;
                    rpDesc.colorAttachments[0].texture     = res.Get(rgSwap);
                    rpDesc.colorAttachments[0].clearOnLoad = false;
                    rpDesc.depthAttachment.texture         = res.Get(rgDepth);
                    rpDesc.depthAttachment.clearOnLoad     = false;
                    rpDesc.hasDepth = true;
                    rpDesc.width    = w;
                    rpDesc.height   = h;
                    cmd.BeginRenderPass(rpDesc);
                    cmd.SetViewport(RHI::RHIViewport{0.f, 0.f, float(w), float(h)});
                    cmd.SetScissor(RHI::RHIScissor{0, 0, w, h});
                    cmd.SetPipeline(pipeline);
                    cmd.SetDescriptorSet(0, frameSet);
                    cmd.SetDescriptorSet(1, descSet);
                    cmd.SetPushConstants(&viewProj, sizeof(glm::mat4), RHI::RHIShaderStage::Vertex);
                    cmd.Draw(vertCount, 1, 0, 0);
                    cmd.EndRenderPass();
                });
        }
    }

    // ── Always-on-top (xray) lines — no depth test ────────────────────────────
    const auto     xrayVerts      = dd->GetOverlayVertices();
    const uint32_t xrayVertCount  = static_cast<uint32_t>(xrayVerts.size());
    if (xrayVertCount > 0 && m_xrayType && m_xraySsbo.IsValid()) {
        ctx.device->UploadBufferData(m_xraySsbo, xrayVerts.data(),
                                     xrayVertCount * sizeof(DebugDraw::Vertex));

        const RHI::RHIPipelineHandle xrayPipeline = m_xrayType->GetOrCreatePipeline(ctx.device, xrayKey);
        if (xrayPipeline.IsValid()) {
            const RHI::RHIDescSetHandle frameSet    = ctx.frameSet;
            const RHI::RHIDescSetHandle xrayDescSet = m_xrayDescSet;
            // No depth attachment: depthTest=false, depthWrite=false in the pipeline.
            ctx.rg->AddPass("DebugOverlayXray",
                [rgSwap](RGPassBuilder& b) {
                    b.Read(rgSwap); b.Write(rgSwap);
                },
                [xrayPipeline, frameSet, xrayDescSet, viewProj, xrayVertCount, rgSwap, w, h]
                (RHI::IRHICommandList& cmd, const RGResources& res)
                {
                    RHI::RHIRenderPassDesc rpDesc{};
                    rpDesc.colorAttachmentCount            = 1;
                    rpDesc.colorAttachments[0].texture     = res.Get(rgSwap);
                    rpDesc.colorAttachments[0].clearOnLoad = false;
                    rpDesc.hasDepth = false;
                    rpDesc.width    = w;
                    rpDesc.height   = h;
                    cmd.BeginRenderPass(rpDesc);
                    cmd.SetViewport(RHI::RHIViewport{0.f, 0.f, float(w), float(h)});
                    cmd.SetScissor(RHI::RHIScissor{0, 0, w, h});
                    cmd.SetPipeline(xrayPipeline);
                    cmd.SetDescriptorSet(0, frameSet);
                    cmd.SetDescriptorSet(1, xrayDescSet);
                    cmd.SetPushConstants(&viewProj, sizeof(glm::mat4), RHI::RHIShaderStage::Vertex);
                    cmd.Draw(xrayVertCount, 1, 0, 0);
                    cmd.EndRenderPass();
                });
        }
    }
}

// ── SelectionMaskFeature ──────────────────────────────────────────────────────

void SceneRenderer::SelectionMaskFeature::OnInit(const FeatureInitContext& ctx)
{
    if (!ctx.matMgr->RegisterTypeFromShaders(
            {"SelectionMask", "selection_mask", "selection_mask",
             RHI::RHICullMode::Back, RHI::RHIBlendMode::Opaque,
             RHI::RHITopology::TriangleList, false, false, false}, ctx)) {
        SA_LOG_WARN("SelectionMaskFeature: shader load failed");
        return;
    }
    m_type = ctx.matMgr->GetType("SelectionMask");
    if (!m_type) SA_LOG_WARN("SelectionMaskFeature: type not found after register");
}

void SceneRenderer::SelectionMaskFeature::AddPasses(
    SceneRenderer& renderer, const FrameContext& ctx,
    const RendererHandles& handles, const entt::registry& reg,
    uint32_t w, uint32_t h)
{
    if (!m_type || renderer.m_selectionEntity == entt::null) return;

    // BFS: collect the selected entity and all descendants so child meshes
    // are included in the outline (e.g. selecting a skeleton root outlines all parts).
    std::vector<entt::entity> subtree;
    subtree.push_back(renderer.m_selectionEntity);
    for (size_t i = 0; i < subtree.size(); ++i) {
        if (const auto* hc = reg.try_get<HierarchyComponent>(subtree[i]))
            for (entt::entity child : hc->children)
                subtree.push_back(child);
    }

    struct DC {
        RHI::RHIBufferHandle vb, ib;
        uint32_t firstIndex, indexCount;
        int32_t  vertexOffset;
        glm::mat4 model;
    };
    std::vector<DC> dcs;
    for (const auto& di : renderer.m_drawItems) {
        bool inSubtree = false;
        for (entt::entity e : subtree) { if (di.entity == e) { inSubtree = true; break; } }
        if (!inSubtree) continue;
        const auto* wt = reg.try_get<WorldTransformComponent>(di.entity);
        if (!wt) continue;
        dcs.push_back({di.vertexBuffer, di.indexBuffer,
                       di.firstIndex, di.indexCount, di.vertexOffset,
                       wt->matrix * di.subLocalTransform});
    }
    if (dcs.empty()) return;

    // No depth attachment — depthTest=false avoids z-fighting with GBuffer geometry,
    // giving a clean filled silhouette with no interior holes or edge artifacts.
    AttachmentKey maskKey{};
    maskKey.colorCount      = 1;
    maskKey.colorFormats[0] = RHI::RHIFormat::R8_UNORM;
    maskKey.depthFormat     = RHI::RHIFormat::Undefined;

    const RHI::RHIPipelineHandle pipeline = m_type->GetOrCreatePipeline(ctx.device, maskKey);
    if (!pipeline.IsValid()) return;

    const RHI::RHIDescSetHandle frameSet = ctx.frameSet;
    const RGTextureHandle       rgMask   = handles.selectionMask;

    ctx.rg->AddPass("SelectionMask",
        [rgMask](RGPassBuilder& b) {
            b.Write(rgMask);
        },
        [pipeline, frameSet, dcs = std::move(dcs), rgMask, w, h]
        (RHI::IRHICommandList& cmd, const RGResources& res)
        {
            RHI::RHIRenderPassDesc rpDesc{};
            rpDesc.colorAttachmentCount            = 1;
            rpDesc.colorAttachments[0].texture     = res.Get(rgMask);
            rpDesc.colorAttachments[0].clearOnLoad = true;
            rpDesc.hasDepth = false;
            rpDesc.width    = w;
            rpDesc.height   = h;

            cmd.BeginRenderPass(rpDesc);
            cmd.SetViewport(RHI::RHIViewport{0.f, 0.f, float(w), float(h)});
            cmd.SetScissor(RHI::RHIScissor{0, 0, w, h});
            cmd.SetPipeline(pipeline);
            cmd.SetDescriptorSet(0, frameSet);
            for (const auto& dc : dcs) {
                cmd.SetVertexBuffer(0, dc.vb);
                cmd.SetIndexBuffer(dc.ib);
                cmd.SetPushConstants(&dc.model, sizeof(glm::mat4),
                                     RHI::RHIShaderStage::Vertex);
                cmd.DrawIndexed(dc.indexCount, 1, dc.firstIndex,
                                dc.vertexOffset, 0);
            }
            cmd.EndRenderPass();
        });
}

// ── SelectionOutlineFeature ───────────────────────────────────────────────────

void SceneRenderer::SelectionOutlineFeature::OnInit(const FeatureInitContext& ctx)
{
    // Pass 1: horizontal max-dilation → R8 intermediate.
    if (!ctx.matMgr->RegisterTypeFromShaders(
            {"SelectionDilateH", "fullscreen_tri", "selection_dilate_h",
             RHI::RHICullMode::None, RHI::RHIBlendMode::Opaque,
             RHI::RHITopology::TriangleList, false, false, true}, ctx)) {
        SA_LOG_WARN("SelectionOutlineFeature: dilate_h shader load failed");
        return;
    }
    m_dilateHType = ctx.matMgr->GetType("SelectionDilateH");
    if (!m_dilateHType) return;
    m_dilateHDescSet = ctx.device->AllocateDescriptorSet(m_dilateHType->shader.GetMaterialLayout());

    // Pass 2: vertical max-dilation + composite → swapchain.
    if (!ctx.matMgr->RegisterTypeFromShaders(
            {"SelectionOutline", "fullscreen_tri", "selection_outline",
             RHI::RHICullMode::None, RHI::RHIBlendMode::AlphaBlend,
             RHI::RHITopology::TriangleList, false, false, true}, ctx)) {
        SA_LOG_WARN("SelectionOutlineFeature: outline shader load failed");
        return;
    }
    m_type = ctx.matMgr->GetType("SelectionOutline");
    if (!m_type) return;
    m_descSet = ctx.device->AllocateDescriptorSet(m_type->shader.GetMaterialLayout());
}

void SceneRenderer::SelectionOutlineFeature::OnShutdown(RHI::IRHIDevice* /*device*/)
{
    m_dilateHDescSet = {};
    m_dilateHType    = nullptr;
    m_descSet        = {};
    m_type           = nullptr;
}

void SceneRenderer::SelectionOutlineFeature::AddPasses(
    SceneRenderer& renderer, const FrameContext& ctx,
    const RendererHandles& handles, const entt::registry& /*reg*/,
    uint32_t w, uint32_t h)
{
    if (!m_dilateHType || !m_dilateHDescSet.IsValid()) return;
    if (!m_type        || !m_descSet.IsValid())        return;
    if (renderer.m_selectionEntity == entt::null) return;

    struct OutlinePC { glm::vec4 texelSize; glm::vec4 outlineColor; float outlineWidth; };
    const OutlinePC pc{
        {1.f / float(w), 1.f / float(h), 0.f, 0.f},
        {1.f, 0.5f, 0.f, 1.f},
        renderer.m_selectionOutlineWidth
    };

    // ── Pass 1: horizontal dilation ───────────────────────────────────────────
    ctx.BindTexture(m_dilateHDescSet, 0, handles.selectionMask);

    AttachmentKey r8Key{};
    r8Key.colorCount      = 1;
    r8Key.colorFormats[0] = RHI::RHIFormat::R8_UNORM;
    r8Key.depthFormat     = RHI::RHIFormat::Undefined;

    const RHI::RHIPipelineHandle dilateHPipeline = m_dilateHType->GetOrCreatePipeline(ctx.device, r8Key);
    if (!dilateHPipeline.IsValid()) return;

    {
        const RHI::RHIDescSetHandle frameSet    = ctx.frameSet;
        const RHI::RHIDescSetHandle dilateHDesc = m_dilateHDescSet;
        const RGTextureHandle       rgMask      = handles.selectionMask;
        const RGTextureHandle       rgDilateH   = handles.dilateH;

        ctx.rg->AddPass("SelectionDilateH",
            [rgMask, rgDilateH](RGPassBuilder& b) {
                b.Read(rgMask);
                b.Write(rgDilateH);
            },
            [dilateHPipeline, frameSet, dilateHDesc, pc, rgDilateH, w, h]
            (RHI::IRHICommandList& cmd, const RGResources& res)
            {
                RHI::RHIRenderPassDesc rpDesc{};
                rpDesc.colorAttachmentCount            = 1;
                rpDesc.colorAttachments[0].texture     = res.Get(rgDilateH);
                rpDesc.colorAttachments[0].clearOnLoad = true;
                rpDesc.width  = w;
                rpDesc.height = h;

                cmd.BeginRenderPass(rpDesc);
                cmd.SetViewport(RHI::RHIViewport{0.f, 0.f, float(w), float(h)});
                cmd.SetScissor(RHI::RHIScissor{0, 0, w, h});
                cmd.SetPipeline(dilateHPipeline);
                cmd.SetDescriptorSet(0, frameSet);
                cmd.SetDescriptorSet(1, dilateHDesc);
                cmd.SetPushConstants(&pc, sizeof(pc), RHI::RHIShaderStage::Fragment);
                cmd.Draw(3, 1, 0, 0);
                cmd.EndRenderPass();
            });
    }

    // ── Pass 2: vertical dilation + composite ─────────────────────────────────
    ctx.BindTexture(m_descSet, 0, handles.dilateH);
    ctx.BindTexture(m_descSet, 1, handles.selectionMask);

    AttachmentKey swapKey{};
    swapKey.colorCount      = 1;
    swapKey.colorFormats[0] = ctx.device->GetSwapchainFormat();
    swapKey.depthFormat     = RHI::RHIFormat::Undefined;

    const RHI::RHIPipelineHandle outlinePipeline = m_type->GetOrCreatePipeline(ctx.device, swapKey);
    if (!outlinePipeline.IsValid()) return;

    {
        const RHI::RHIDescSetHandle frameSet    = ctx.frameSet;
        const RHI::RHIDescSetHandle outlineDesc = m_descSet;
        const RGTextureHandle       rgDilateH   = handles.dilateH;
        const RGTextureHandle       rgMask      = handles.selectionMask;
        const RGTextureHandle       rgSwap      = handles.swapchain;

        ctx.rg->AddPass("SelectionOutline",
            [rgDilateH, rgMask, rgSwap](RGPassBuilder& b) {
                b.Read(rgDilateH);
                b.Read(rgMask);
                b.Read(rgSwap);
                b.Write(rgSwap);
            },
            [outlinePipeline, frameSet, outlineDesc, pc, rgSwap, w, h]
            (RHI::IRHICommandList& cmd, const RGResources& res)
            {
                RHI::RHIRenderPassDesc rpDesc{};
                rpDesc.colorAttachmentCount            = 1;
                rpDesc.colorAttachments[0].texture     = res.Get(rgSwap);
                rpDesc.colorAttachments[0].clearOnLoad = false;
                rpDesc.width  = w;
                rpDesc.height = h;

                cmd.BeginRenderPass(rpDesc);
                cmd.SetViewport(RHI::RHIViewport{0.f, 0.f, float(w), float(h)});
                cmd.SetScissor(RHI::RHIScissor{0, 0, w, h});
                cmd.SetPipeline(outlinePipeline);
                cmd.SetDescriptorSet(0, frameSet);
                cmd.SetDescriptorSet(1, outlineDesc);
                cmd.SetPushConstants(&pc, sizeof(pc), RHI::RHIShaderStage::Fragment);
                cmd.Draw(3, 1, 0, 0);
                cmd.EndRenderPass();
            });
    }
}

// ── InfiniteGridFeature ───────────────────────────────────────────────────────

void SceneRenderer::InfiniteGridFeature::OnInit(const FeatureInitContext& ctx)
{
    if (!ctx.matMgr->RegisterTypeFromShaders(
            {"InfiniteGrid", "infinite_grid", "infinite_grid",
             RHI::RHICullMode::None, RHI::RHIBlendMode::AlphaBlend,
             RHI::RHITopology::TriangleList, true, false, true}, ctx)) {
        SA_LOG_WARN("InfiniteGridFeature: shader load failed");
        return;
    }
    m_type = ctx.matMgr->GetType("InfiniteGrid");
    if (!m_type) SA_LOG_WARN("InfiniteGridFeature: type not found after register");
}

void SceneRenderer::InfiniteGridFeature::AddPasses(
    SceneRenderer& renderer, const FrameContext& ctx,
    const RendererHandles& handles, const entt::registry& /*reg*/,
    uint32_t w, uint32_t h)
{
    if (!m_type || !renderer.m_infiniteGrid) return;

    AttachmentKey key{};
    key.colorCount      = 1;
    key.colorFormats[0] = ctx.device->GetSwapchainFormat();
    key.depthFormat     = RHI::RHIFormat::D32F;

    const RHI::RHIPipelineHandle pipeline = m_type->GetOrCreatePipeline(ctx.device, key);
    if (!pipeline.IsValid()) return;

    const RHI::RHIDescSetHandle frameSet = ctx.frameSet;
    const RGTextureHandle       rgSwap   = handles.swapchain;
    const RGTextureHandle       rgDepth  = handles.depth;

    ctx.rg->AddPass("InfiniteGrid",
        [rgSwap, rgDepth](RGPassBuilder& b) {
            b.Read(rgSwap);
            b.Write(rgSwap);
            b.WriteDepth(rgDepth);
        },
        [pipeline, frameSet, rgSwap, rgDepth, w, h]
        (RHI::IRHICommandList& cmd, const RGResources& res)
        {
            RHI::RHIRenderPassDesc rpDesc{};
            rpDesc.colorAttachmentCount            = 1;
            rpDesc.colorAttachments[0].texture     = res.Get(rgSwap);
            rpDesc.colorAttachments[0].clearOnLoad = false;
            rpDesc.depthAttachment.texture         = res.Get(rgDepth);
            rpDesc.depthAttachment.clearOnLoad     = false;
            rpDesc.hasDepth = true;
            rpDesc.width    = w;
            rpDesc.height   = h;

            cmd.BeginRenderPass(rpDesc);
            cmd.SetViewport(RHI::RHIViewport{0.f, 0.f, float(w), float(h)});
            cmd.SetScissor(RHI::RHIScissor{0, 0, w, h});
            cmd.SetPipeline(pipeline);
            cmd.SetDescriptorSet(0, frameSet);
            cmd.Draw(3, 1, 0, 0);
            cmd.EndRenderPass();
        });
}

} // namespace StellarAlia
