#include "function/renderer/SceneRenderer.hpp"

#include "core/logs/Log.hpp"
#include "core/Profiler.hpp"
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
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <span>
#include <unordered_set>

namespace StellarAlia {

static std::vector<uint8_t> LoadComputeSpv(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { SA_LOG_ERROR("SceneRenderer: cannot open shader '{}'", path); return {}; }
    const auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> data(sz);
    f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(sz));
    return data;
}

static RHI::ShaderReflection LoadComputeRefl(const std::string& path) {
    RHI::ShaderReflection r;
    if (!RHI::ShaderReflectionIO::LoadFromFile(path, r))
        SA_LOG_ERROR("SceneRenderer: cannot load reflection '{}'", path);
    return r;
}

// Arvo AABB transformation: transforms a local AABB by a matrix using 3 column
// operations instead of transforming 8 corners. O(3) vec3 muls.
static void ArvoAABB(const glm::vec3& lMin, const glm::vec3& lMax,
                     const glm::mat4& M,
                     glm::vec3& outMin, glm::vec3& outMax) {
    outMin = outMax = glm::vec3(M[3]);
    for (int i = 0; i < 3; ++i) {
        const glm::vec3 col = glm::vec3(M[i]);
        outMin += glm::min(col * lMin[i], col * lMax[i]);
        outMax += glm::max(col * lMin[i], col * lMax[i]);
    }
}

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

    // Per-frame SSBO ring for material parameters (Issue #72).
    if (!m_materialRing.Init(desc.device)) {
        SA_LOG_ERROR("SceneRenderer: MaterialParamRing init failed");
        return false;
    }
    // MaterialManager needs the ring buffer handle before any MaterialType is
    // registered (RegisterTypeFromShaders → CreateInstance writes the descriptor).
    desc.matMgr->SetMaterialParamRingBuffer(m_materialRing.GetBuffer());

    // IBL bake — initialise so SetIBL can fall back to GPU bake on cache miss.
    if (!m_iblBake.Init(desc.device, desc.shaderDir)) {
        SA_LOG_WARN("SceneRenderer: GpuIblBake init failed — IBL bake unavailable");
    } else {
        // Pre-bake the BRDF LUT immediately (no HDR needed).
        // m_bakeBrdfLut is the authoritative copy — it lives outside ResourceManager
        // and is never destroyed by ClearProjectAssets.  m_cachedBrdfLut is restored
        // to it by ResetProjectIBL() after each project switch.
        m_bakeBrdfLut   = m_iblBake.BakeBrdfLut(desc.device);
        m_cachedBrdfLut = m_bakeBrdfLut;
    }

    // LTC LUT upload — always succeeds if device is valid; data is embedded.
    m_ltcBake.Upload(desc.device);
    m_frameUniforms.SetLtcTextures(m_ltcBake.GetLtcMat(), m_ltcBake.GetLtcAmp());

    // ── Build FeatureInitContext (shared by Init and all feature OnInits) ────────
    const FeatureInitContext ctx{desc.device, desc.matMgr, desc.resMgr,
                                 frameLayout, desc.shaderDir, desc.shaderDir};

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
        // HDR_Color is now a transient RG texture — no persistent handle needed.
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
        m_ssaoTex        = makeR8("SSAO_Result");
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
    //   [Shadow?, Skybox, GBuffer, SSAO, DeferredLighting, SelectionMask, TAA, Bloom, Tonemap,
    //    ...user features, SelectionOutline, DebugOverlay]
    {
        auto tf = std::make_unique<TonemapFeature>();
        m_tonemapFeature = tf.get();
        m_features.insert(m_features.begin(), std::move(tf));
    }
    {
        // DoF runs after Bloom but before Tonemap: inserted between them.
        auto dofF = std::make_unique<DoFFeature>();
        m_dofFeature = dofF.get();
        m_features.insert(m_features.begin(), std::move(dofF));
    }
    {
        auto bf = std::make_unique<BloomFeature>(m_bloomMipCount);
        m_bloomFeature = bf.get();
        m_features.insert(m_features.begin(), std::move(bf));
    }
    {
        // TAA runs before Bloom: writes to history (pre-bloom), exposes handle via
        // m_outputHandle for BloomThreshold to read. BloomComposite then rewrites
        // handles.hdr (transient HDR_Color) so Tonemap reads bloom+anti-aliased from there.
        auto taaF = std::make_unique<TAAFeature>();
        m_taaFeature = taaF.get();
        m_features.insert(m_features.begin(), std::move(taaF));
    }
    {
        // AutoExposure runs between TAA and Bloom: reads raw HDR, writes to persistent
        // exposure SSBO; CPU reads back the result at the start of the next frame.
        auto aeF = std::make_unique<AutoExposureFeature>();
        m_aeFeature = aeF.get();
        m_features.insert(m_features.begin() + 1, std::move(aeF));
    }
    {
        auto dlF = std::make_unique<DeferredLightingFeature>();
        m_deferredLightingFeature = dlF.get();
        m_features.insert(m_features.begin(), std::move(dlF));
    }
    // SelectionMask runs immediately after DeferredLighting (depth is populated,
    // already transitioned back to depth-attachment by this WriteDepth declaration).
    m_features.insert(m_features.begin() + 1, std::make_unique<SelectionMaskFeature>(this));
    {
        auto ssaoF = std::make_unique<SSAOFeature>();
        m_ssaoFeature = ssaoF.get();
        m_features.insert(m_features.begin(), std::move(ssaoF));
    }
    {
        auto gf = std::make_unique<GBufferFeature>(this);
        m_gbufferFeature = gf.get();
        m_features.insert(m_features.begin(), std::move(gf));
    }
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
    for (int i = 0; i < m_bloomMipCount; ++i)
        if (m_bloomMip[i].IsValid()) m_device->DestroyTexture(m_bloomMip[i]);

    if (m_selectionMask.IsValid()) m_device->DestroyTexture(m_selectionMask);
    if (m_dilateH.IsValid())       m_device->DestroyTexture(m_dilateH);
    if (m_ssaoTex.IsValid())       m_device->DestroyTexture(m_ssaoTex);

    if (m_iblBake.IsInitialized())
        m_iblBake.Shutdown(m_device);
    if (m_ltcBake.IsUploaded())
        m_ltcBake.Shutdown(m_device);
    if (m_depthTex.IsValid())
        m_device->DestroyTexture(m_depthTex);
    if (m_solidAmbientCube.IsValid())
        m_device->DestroyTexture(m_solidAmbientCube);

    m_frameUniforms.Shutdown();
    m_materialRing.Shutdown();
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
    const PostProcessSettings& pp = ws.pp;

    // Update skybox background mode + color immediately (no GPU work needed).
    if (m_skyboxFeature) {
        m_skyboxFeature->m_backgroundMode  = ws.backgroundMode;
        m_skyboxFeature->m_backgroundColor = ws.backgroundColor;
    }

    // Bloom — update runtime parameters.
    if (m_bloomFeature) {
        m_bloomFeature->m_enabled   = pp.bloomEnabled;
        m_bloomFeature->m_threshold = pp.bloomThreshold;
        m_bloomFeature->m_strength  = pp.bloomStrength;
        m_bloomFeature->m_radius    = pp.bloomRadius;

        const int newMipLevels = std::clamp(pp.bloomMipLevels, 2, kMaxBloomMips);
        if (newMipLevels != m_bloomMipCount && newMipLevels != m_pendingBloomMipCount) {
            // Defer the actual GPU rebuild to the start of the next RenderFrame where
            // WaitIdle is safe (here we're between Execute and EndFrame — use-after-free risk).
            m_pendingBloomMipCount = newMipLevels;
            m_depthWidth = m_depthHeight = 0;  // trigger resize block next frame
        }
    }

    // SSAO (GTAO) — update runtime parameters.
    if (m_ssaoFeature) {
        m_ssaoFeature->m_enabled       = pp.ssaoEnabled;
        m_ssaoFeature->m_radius        = pp.ssaoRadius;
        m_ssaoFeature->m_strength      = pp.ssaoStrength;
        m_ssaoFeature->m_bias          = pp.ssaoBias;
        m_ssaoFeature->m_directions    = pp.ssaoDirections;
        m_ssaoFeature->m_steps         = pp.ssaoSteps;
        m_ssaoFeature->m_blurSharpness = pp.ssaoBlurSharpness;
    }

    // Auto Exposure — update runtime parameters.
    if (m_aeFeature) {
        m_aeFeature->m_enabled    = pp.autoExposureEnabled;
        m_aeFeature->m_evMin      = pp.aeEvMin;
        m_aeFeature->m_evMax      = pp.aeEvMax;
        m_aeFeature->m_adaptSpeed = pp.aeAdaptSpeed;
        m_aeFeature->m_lowPct     = pp.aeLowPercent;
        m_aeFeature->m_highPct    = pp.aeHighPercent;
    }

    // Depth of Field — update runtime parameters.
    if (m_dofFeature) {
        m_dofFeature->m_enabled     = pp.dofEnabled;
        m_dofFeature->m_focusDist   = pp.focusDistance;
        m_dofFeature->m_aperture    = pp.aperture;
        m_dofFeature->m_focalLength = pp.focalLength;
        m_dofFeature->m_samples     = pp.dofSamples;
        m_dofFeature->m_maxCocPx    = pp.maxCocPx;
    }

    // TAA — update runtime parameters; reset history on enable/disable toggle.
    if (m_taaFeature) {
        const bool wasEnabled = m_taaFeature->m_enabled;
        m_taaFeature->m_enabled      = pp.taaEnabled;
        m_taaFeature->m_blendStatic  = pp.taaBlendStatic;
        m_taaFeature->m_blendMotion  = pp.taaBlendMotion;
        m_taaFeature->m_antiGhosting = pp.taaAntiGhosting;
        if (wasEnabled != pp.taaEnabled)
            m_taaFeature->m_historyValid = false;  // discard stale history on toggle
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
                                  m_frameUniforms.GetLayout(), m_shaderDir, m_shaderDir};

    if (pp.tonemapMode == PostProcessSettings::TonemapMode::Builtin) {
        if (auto* tf = dynamic_cast<TonemapFeature*>(m_tonemapFeature)) {
            tf->m_exposure = pp.exposure;
            tf->SetColorGrading(pp.colorGrading);
        } else {
            // Replace LUT → Builtin
            m_device->WaitIdle();
            auto newFeature = std::make_unique<TonemapFeature>();
            newFeature->m_exposure = pp.exposure;
            ReplaceTonemapFeature(std::move(newFeature), ctx);
            static_cast<TonemapFeature*>(m_tonemapFeature)->SetColorGrading(pp.colorGrading);
        }
    } else {
        // LUT tonemap — requires a valid, loaded texture.
        // If no LUT is set, fall back to the builtin ACES pipeline instead.
        RHI::RHITextureHandle lutTex;
        if (pp.tonemapLut.IsValid())
            lutTex = m_resMgr->LoadTexture(pp.tonemapLut);

        if (!lutTex.IsValid()) {
            SA_LOG_WARN("SceneRenderer: LUT mode requested but no valid LUT texture — falling back to builtin tonemap");
            if (!dynamic_cast<TonemapFeature*>(m_tonemapFeature)) {
                m_device->WaitIdle();
                auto newFeature = std::make_unique<TonemapFeature>();
                newFeature->m_exposure = pp.exposure;
                ReplaceTonemapFeature(std::move(newFeature), ctx);
                static_cast<TonemapFeature*>(m_tonemapFeature)->SetColorGrading(pp.colorGrading);
            } else if (auto* tf = dynamic_cast<TonemapFeature*>(m_tonemapFeature)) {
                tf->m_exposure = pp.exposure;
                tf->SetColorGrading(pp.colorGrading);
            }
            return;
        }

        if (auto* lf = dynamic_cast<LutTonemapFeature*>(m_tonemapFeature)) {
            lf->m_exposure    = pp.exposure;
            lf->m_lutStrength = pp.lutStrength;
            lf->SetLutTexture(m_device, lutTex);
        } else {
            // Replace Builtin → LUT
            m_device->WaitIdle();
            auto newFeature = std::make_unique<LutTonemapFeature>();
            newFeature->m_exposure    = pp.exposure;
            newFeature->m_lutStrength = pp.lutStrength;
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

void SceneRenderer::ResetProjectIBL()
{
    // Restore the BRDF LUT to the one baked at Init time.  Called after
    // ClearProjectAssets() so the next ApplyWorldSettings(SolidColor) writes a
    // valid VkImageView instead of a dangling handle from the previous project.
    m_cachedBrdfLut = m_bakeBrdfLut;
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
    SA_PROFILE_SCOPE_N("BuildDrawList");
    m_drawItems.clear();
    m_bvh.Clear();

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
            const WorldTransformComponent& wt)
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

        // Entity-level world AABB = union of all submesh world AABBs.
        glm::vec3 entityWorldMin( 1e30f);
        glm::vec3 entityWorldMax(-1e30f);

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

            ArvoAABB(sub.boundsMin, sub.boundsMax, sub.localTransform,
                     item.localAABBMin, item.localAABBMax);

            // Accumulate entity-level world AABB (union of all submeshes).
            ArvoAABB(sub.boundsMin, sub.boundsMax, wt.matrix * sub.localTransform,
                     item.worldAABBMin, item.worldAABBMax);
            entityWorldMin = glm::min(entityWorldMin, item.worldAABBMin);
            entityWorldMax = glm::max(entityWorldMax, item.worldAABBMax);

            item.material = base;
            if (base->GetType()->usesMaterialParamsSSBO) {
                // Issue #72: SSBO path — every draw call (override or not) packs
                // a blob into the per-frame ring and binds set=1 with a dynamic
                // offset. No MaterialInstance clone, no per-entity descriptor.
                std::vector<uint8_t> blob = base->GetParamBlob();
                if (matOverride) {
                    for (const auto& [name, val] : matOverride->scalars) {
                        const ParamDef* pd = base->GetType()->FindParam(name);
                        if (!pd) continue;
                        std::visit([&](const auto& v) {
                            const uint32_t sz = static_cast<uint32_t>(sizeof(v));
                            if (pd->offset + sz <= blob.size())
                                std::memcpy(blob.data() + pd->offset, &v, sz);
                        }, val);
                    }
                    for (const auto& [name, texID] : matOverride->textures) {
                        if (!texID.IsValid()) continue;
                        const TextureDef* td = base->GetType()->FindTexture(name);
                        if (!td) continue;
                        const auto tex = m_resMgr->LoadTexture(texID);
                        if (!tex.IsValid()) continue;
                        const uint32_t idx = m_matMgr->GetTextureHeap().Register(tex);
                        if (td->uboBlobOffset + sizeof(idx) <= blob.size())
                            std::memcpy(blob.data() + td->uboBlobOffset, &idx, sizeof(idx));
                    }
                }
                item.materialUboOffset =
                    m_materialRing.Allocate(blob.data(),
                                            static_cast<uint32_t>(blob.size()));
            } else if (matOverride && (!matOverride->scalars.empty() || !matOverride->textures.empty())) {
                // Legacy UBO path with overrides — still uses CloneInstance.
                // Triggers the original validation issue but only for non-PBR
                // shaders that haven't been converted yet (none in current builtin set).
                auto clone = m_matMgr->CloneInstance(base);
                if (clone) {
                    for (const auto& [name, val] : matOverride->scalars)
                        std::visit([&](const auto& v){ clone->SetParam(name, v); }, val);
                    for (const auto& [name, texID] : matOverride->textures)
                        if (texID.IsValid())
                            clone->SetTexture(name, m_resMgr->LoadTexture(texID));
                    item.material      = clone.get();
                    item.ownedMaterial = std::move(clone);
                }
            }

            item.pipeline         = item.material->GetType()->GetOrCreatePipeline(m_device, gbKey);
            item.pushConstantSize = static_cast<uint32_t>(sizeof(glm::mat4));

            m_drawItems.push_back(std::move(item));
        }

        // One BVH leaf per entity.
        if (entityWorldMin.x < 1e29f)
            m_bvh.Insert(entityWorldMin, entityWorldMax, e);
    });

    // ── Skinned meshes ────────────────────────────────────────────────────────
    // Shared geometry (vertexBuffer, indexBuffer, subMeshes) comes from GPUMesh.
    // Per-entity skinDescSet (skinMatricesBuffer + skinDataBuffer) from SkinnedMeshComponent.
    scene.View<SkinnedMeshComponent, WorldTransformComponent>().each(
        [&](entt::entity e,
            const SkinnedMeshComponent&    meshComp,
            const WorldTransformComponent& wt)
    {
        if (!meshComp.ready) return;

        const Resource::GPUMesh* gpuMesh = m_resMgr->LoadMesh(meshComp.meshAsset);
        if (!gpuMesh) return;

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
            item.subLocalTransform = glm::mat4(1.f);  // skeleton drives transforms
            item.vertexBuffer      = gpuMesh->vertexBuffer;
            item.indexBuffer       = gpuMesh->indexBuffer;
            item.firstIndex        = sub.firstIndex;
            item.indexCount        = sub.indexCount;
            item.vertexOffset      = sub.vertexOffset;
            item.skipCull          = true; // animated AABB not computed; always render
            item.isSkinned         = true;
            item.skinDescSet       = meshComp.skinDescSet;
            // Approximate world AABB from bind-pose bounds × world transform.
            ArvoAABB(sub.boundsMin, sub.boundsMax, wt.matrix,
                     item.worldAABBMin, item.worldAABBMax);

            item.material = base;
            if (base->GetType()->usesMaterialParamsSSBO) {
                // Issue #72: same SSBO+bindless path as static meshes.
                std::vector<uint8_t> blob = base->GetParamBlob();
                if (matOverride) {
                    for (const auto& [name, val] : matOverride->scalars) {
                        const ParamDef* pd = base->GetType()->FindParam(name);
                        if (!pd) continue;
                        std::visit([&](const auto& v) {
                            const uint32_t sz = static_cast<uint32_t>(sizeof(v));
                            if (pd->offset + sz <= blob.size())
                                std::memcpy(blob.data() + pd->offset, &v, sz);
                        }, val);
                    }
                    for (const auto& [name, texID] : matOverride->textures) {
                        if (!texID.IsValid()) continue;
                        const TextureDef* td = base->GetType()->FindTexture(name);
                        if (!td) continue;
                        const auto tex = m_resMgr->LoadTexture(texID);
                        if (!tex.IsValid()) continue;
                        const uint32_t idx = m_matMgr->GetTextureHeap().Register(tex);
                        if (td->uboBlobOffset + sizeof(idx) <= blob.size())
                            std::memcpy(blob.data() + td->uboBlobOffset, &idx, sizeof(idx));
                    }
                }
                item.materialUboOffset =
                    m_materialRing.Allocate(blob.data(),
                                            static_cast<uint32_t>(blob.size()));
            } else if (matOverride && (!matOverride->scalars.empty() || !matOverride->textures.empty())) {
                auto clone = m_matMgr->CloneInstance(base);
                if (clone) {
                    for (const auto& [name, val] : matOverride->scalars)
                        std::visit([&](const auto& v){ clone->SetParam(name, v); }, val);
                    for (const auto& [name, texID] : matOverride->textures)
                        if (texID.IsValid())
                            clone->SetTexture(name, m_resMgr->LoadTexture(texID));
                    item.material      = clone.get();
                    item.ownedMaterial = std::move(clone);
                }
            }

            // pipeline is the material's standard pipeline — used for sorting and as
            // a fallback; GBufferFeature overrides with skinnedPipeline at draw time.
            item.pipeline         = item.material->GetType()->GetOrCreatePipeline(m_device, gbKey);
            item.pushConstantSize = static_cast<uint32_t>(sizeof(glm::mat4));

            m_drawItems.push_back(std::move(item));
        }
    });

    // Sort by pipeline first (minimises vkCmdBindPipeline), then vertex buffer
    // (minimises vkCmdBindVertexBuffers).  Both are expensive Vulkan calls that
    // the driver cannot elide without explicit deduplication on the CPU side.
    std::sort(m_drawItems.begin(), m_drawItems.end(),
        [](const DrawItem& a, const DrawItem& b) {
            if (a.pipeline.index != b.pipeline.index)
                return a.pipeline.index < b.pipeline.index;
            return a.vertexBuffer.index < b.vertexBuffer.index;
        });

    m_bvh.Build();

    SA_LOG_INFO("SceneRenderer: built {} draw item(s)", m_drawItems.size());
}

// ── RaycastScene / GetSkinDescLayout ─────────────────────────────────────────

entt::entity SceneRenderer::RaycastScene(const Core::Ray& ray, float maxDist) const {
    float        bestT   = maxDist;
    entt::entity best    = entt::null;

    // Phase A: BVH for static meshes.
    m_bvh.Raycast(ray, maxDist, bestT, best);

    // Phase B: brute-force slab test for skinned meshes (skipCull items not in BVH).
    for (const DrawItem& item : m_drawItems) {
        if (!item.skipCull) continue;
        float t;
        if (Core::BVHTree<entt::entity>::RayAABB(ray, item.worldAABBMin, item.worldAABBMax, bestT, t) && t < bestT) {
            bestT = t;
            best  = item.entity;
        }
    }

    return best;
}

RHI::RHIDescLayoutHandle SceneRenderer::GetSkinDescLayout() const {
    if (!m_gbufferFeature) return {};
    return m_gbufferFeature->m_skinDescLayout;
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

static float HaltonSeq(uint32_t i, uint32_t base) {
    float result = 0.f, f = 1.f;
    while (i > 0) { f /= float(base); result += f * float(i % base); i /= base; }
    return result;
}

void SceneRenderer::ApplyCameraToUniforms(const CameraData& cam, FrameUniforms& fu,
                                           uint32_t w, uint32_t h)
{
    const glm::mat4 unjitteredProj = cam.proj;
    const glm::mat4 invUnjitteredProj = glm::inverse(unjitteredProj);

    fu.view        = cam.view;
    fu.cameraPos   = cam.worldPosition;
    // Inverse matrices use the unjittered projection so that world-space reconstruction
    // (depth → world pos in TAA, SSAO, deferred lighting) is correct.
    fu.invProj     = invUnjitteredProj;
    fu.invViewProj = RigidBodyInverse(cam.view) * invUnjitteredProj;

    // Previous-frame unjittered VP for TAA reprojection.
    fu.prevViewProj          = m_prevUnjitteredViewProj;
    m_prevUnjitteredViewProj = unjitteredProj * cam.view;

    // Sub-pixel Halton(2,3) jitter — only applied when TAA is active.
    glm::mat4 proj = unjitteredProj;
    glm::vec2 jitterPx{0.f};
    if (m_taaFeature && m_taaFeature->m_enabled && w > 0 && h > 0) {
        const uint32_t si = (m_haltonIndex % 16) + 1;  // samples 1..16 avoid (0,0)
        jitterPx.x = HaltonSeq(si, 2) - 0.5f;
        jitterPx.y = HaltonSeq(si, 3) - 0.5f;
        proj[2][0] += 2.f * jitterPx.x / float(w);
        proj[2][1] += 2.f * jitterPx.y / float(h);
    }
    fu.proj      = proj;
    fu.viewProj  = proj * cam.view;
    fu.jitter    = jitterPx;
    fu.frameIndex = m_frameIndex;

    m_haltonIndex = (m_haltonIndex + 1) & 0xFF;
    m_frameIndex  = (m_frameIndex  + 1) & 0xFF;
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
    SA_PROFILE_SCOPE_N("RenderFrame");
    // Per-frame material param ring rolls over here — must happen before any
    // BuildDrawList / feature pass that calls m_materialRing.Allocate().
    m_materialRing.Reset();

    // ── Resize G-Buffer + depth if viewport changed ────────────────────────────
    if (w != m_depthWidth || h != m_depthHeight) {
        m_device->WaitIdle();
        m_rg.InvalidateSlots(*m_device);

        // Pending bloom mip count change deferred from ApplyWorldSettings.
        // WaitIdle above makes GPU work safe here.
        if (m_pendingBloomMipCount != -1) {
            for (int i = 0; i < m_bloomMipCount; ++i) {
                if (m_bloomMip[i].IsValid()) { m_device->DestroyTexture(m_bloomMip[i]); m_bloomMip[i] = {}; }
            }
            m_bloomMipCount = m_pendingBloomMipCount;
            m_pendingBloomMipCount = -1;
            if (m_bloomFeature) m_bloomFeature->RebuildDescSets(m_bloomMipCount, m_device);
        }

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
        // HDR_Color is transient — InvalidateSlots above already freed the slot;
        // AllocateSlots will create a new RGBA16F slot at the new resolution.
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

            // SSAO result at half resolution — all three SSAO passes run at (w/2)×(h/2).
            // DeferredLighting reads it via bilinear sampler and auto-upsamples.
            {
                if (m_ssaoTex.IsValid()) m_device->DestroyTexture(m_ssaoTex);
                RHI::RHITextureDesc d{};
                d.width     = std::max(1u, w / 2);
                d.height    = std::max(1u, h / 2);
                d.format    = RHI::RHIFormat::R8_UNORM;
                d.usage     = RHI::RHITextureUsage::RenderTarget
                            | RHI::RHITextureUsage::Sampled;
                d.debugName = "SSAO_Result";
                m_ssaoTex = m_device->CreateTexture(d);
            }
        }
    }

    // ── Phase 1: collect frame data ───────────────────────────────────────────
    FrameUniforms fu{};
    fu.resolution = {static_cast<float>(w), static_cast<float>(h)};
    fu.time       = static_cast<float>(m_frameCount) / 60.f;
    std::copy(std::begin(m_shCoeffs), std::end(m_shCoeffs), std::begin(fu.irrSH));
    ApplyCameraToUniforms(camera, fu, w, h);
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
    { SA_PROFILE_SCOPE_N("GPU::BeginFrame"); m_cmd = m_device->BeginFrame(); }
    if (!m_cmd) return;

    // Auto Exposure CPU readback: the previous frame's GPU-computed exposure value
    // is now visible on the CPU (fence was waited in BeginFrame).  Update tonemap.
    if (m_aeFeature && m_aeFeature->m_enabled) {
        m_aeFeature->ReadbackExposure(m_device);
        const float ae = m_aeFeature->m_currentExposure;
        if (auto* tf = dynamic_cast<TonemapFeature*>(m_tonemapFeature))
            tf->m_exposure = ae;
        else if (auto* lf = dynamic_cast<LutTonemapFeature*>(m_tonemapFeature))
            lf->m_exposure = ae;
    }

    // Rebuild draw-list after BeginFrame so the fence-wait has already retired
    // any GPU work that held references to the previous draw-items.
    if (scene.IsAndClearMaterialDirty()) BuildDrawList(scene);

    // ── CPU frustum cull ─────────────────────────────────────────────────────
    {
        SA_PROFILE_SCOPE_N("FrustumCull");
        const Core::Frustum frustum = Core::Frustum::Extract(m_currentViewProj);

        m_visibleEntities.clear();
        m_bvh.Query(frustum, m_visibleEntities);

        std::unordered_set<uint32_t> visSet;
        visSet.reserve(m_visibleEntities.size());
        for (const auto e : m_visibleEntities)
            visSet.insert(static_cast<uint32_t>(e));

        m_visibleDrawItems.clear();
        m_visibleDrawItems.reserve(m_drawItems.size());
        for (const auto& item : m_drawItems) {
            if (item.skipCull || visSet.count(static_cast<uint32_t>(item.entity)))
                m_visibleDrawItems.push_back(&item);
        }
        SA_PROFILE_PLOT("VisibleDrawItems", static_cast<double>(m_visibleDrawItems.size()));
    }

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
    {
        RHI::RHITextureDesc d{};
        d.width     = w;
        d.height    = h;
        d.format    = RHI::RHIFormat::RGBA16F;
        d.usage     = RHI::RHITextureUsage::RenderTarget | RHI::RHITextureUsage::Sampled;
        d.debugName = "HDR_Color";
        m_rgHdr = m_rg.CreateTexture("HDR_Color", d);
    }
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
    m_rgSsaoTex = m_rg.ImportTexture("SSAO_Result", m_ssaoTex,
        RHI::RHIResourceState::Undefined, RHI::RHIResourceState::Undefined);

    // ── Build RendererHandles (RG handles for all built-in render targets) ───────
    RendererHandles handles{};
    handles.hdr           = m_rgHdr;
    handles.taaResolved   = m_rgHdr;  // default when TAA disabled; overridden below after TAAFeature runs
    handles.swapchain     = m_rgSwapchain;
    handles.depth         = m_rgDepth;
    handles.gbufferRT0    = m_rgGbRT0;
    handles.gbufferRT1    = m_rgGbRT1;
    handles.gbufferRT2    = m_rgGbRT2;
    handles.shadowMap     = m_rgShadowMap;
    handles.selectionMask = m_rgSelectionMask;
    handles.dilateH       = m_rgDilateH;
    handles.ssaoTex       = m_rgSsaoTex;
    handles.bloomMipCount = m_bloomMipCount;
    for (int i = 0; i < m_bloomMipCount; ++i)
        handles.bloomMip[i] = m_rgBloomMip[i];

    FrameContext ctx{};
    ctx.rg       = &m_rg;
    ctx.frameSet = m_frameDescSet;
    ctx.device   = m_device;

    // ── All passes: [Shadow, Skybox, GBuffer, SSAO, DeferredLighting, SelectionMask, TAA, Bloom, DoF, Tonemap, ...] ─
    // After TAAFeature: redirect handles.taaResolved to TAA history output (BloomThreshold reads it).
    // After DoFFeature: redirect handles.hdr to DoF output (Tonemap reads the DoF-processed frame).
    for (auto& f : m_features) {
        f->AddPasses(*this, ctx, handles, scene.Registry(), w, h);
        if (m_taaFeature && f.get() == m_taaFeature && m_taaFeature->m_outputHandle.IsValid())
            handles.taaResolved = m_taaFeature->m_outputHandle;
        if (m_dofFeature && f.get() == m_dofFeature && m_dofFeature->m_outputHandle.IsValid())
            handles.hdr = m_dofFeature->m_outputHandle;
    }

    // ── Compile + Execute + Present ───────────────────────────────────────────
    { SA_PROFILE_SCOPE_N("RG::Compile");  m_rg.Compile(); }
    // AllocateSlots before FlushBindings: transient slot handles must be valid
    // when we call WriteDescriptorTexture, which must happen before Execute()
    // records vkCmdBindDescriptorSets (descriptor sets must be valid at record time).
    m_rg.AllocateSlots(*m_device);
    ctx.FlushBindings();  // write all queued descriptor updates; AllocateSlots ensures transient handles are valid
    { SA_PROFILE_SCOPE_N("RG::Execute");  m_rg.Execute(*m_device, *m_cmd); }  // AllocateSlots inside Execute is a no-op

    if (uiPass)
        uiPass(m_cmd);

    { SA_PROFILE_SCOPE_N("GPU::Present"); m_device->EndFrame(); m_device->Present(); }

    m_cmd = nullptr;
    ++m_frameCount;
}

// ── FrameContext::BindTexture / BindBuffer / FlushBindings ────────────────────

void FrameContext::BindTexture(RHI::RHIDescSetHandle set, uint32_t binding,
                               RGTextureHandle handle) const
{
    if (!handle.IsValid() || !set.IsValid()) return;
    m_pendingBindings.push_back({set, binding, handle});
}

void FrameContext::BindBuffer(RHI::RHIDescSetHandle set, uint32_t binding,
                              RGBufferHandle handle) const
{
    if (!handle.IsValid() || !set.IsValid()) return;
    m_pendingBufferBindings.push_back({set, binding, handle});
}

void FrameContext::FlushBindings() const
{
    for (const auto& b : m_pendingBindings) {
        const RHI::RHITextureHandle rhi = rg->GetResolvedHandle(b.handle);
        if (rhi.IsValid())
            device->WriteDescriptorTexture(b.set, b.binding, rhi);
    }
    m_pendingBindings.clear();

    for (const auto& b : m_pendingBufferBindings) {
        const RHI::RHIBufferHandle rhi = rg->GetResolvedBufferHandle(b.handle);
        if (rhi.IsValid())
            device->WriteDescriptorBuffer(b.set, b.binding, rhi);
    }
    m_pendingBufferBindings.clear();
}

// ── ShadowFeature ─────────────────────────────────────────────────────────────

void SceneRenderer::ShadowFeature::OnInit(const FeatureInitContext& ctx)
{
    ctx.matMgr->RegisterTypeFromShaders(
        {"Shadow", "shadow", "shadow",
         RHI::RHICullMode::Front, RHI::RHIBlendMode::Opaque, RHI::RHITopology::TriangleList, true, true, false}, ctx);
    m_type = ctx.matMgr->GetType("Shadow");
    if (!m_type) SA_LOG_WARN("ShadowFeature: shader load failed — shadows disabled");

    if (!ctx.matMgr->LoadShaderProgram(m_skinnedShadowProgram, "shadow_skinned", "shadow", ctx))
        SA_LOG_WARN("ShadowFeature: skinned shadow shader not found — skinned shadows disabled");
}

void SceneRenderer::ShadowFeature::AddPasses(SceneRenderer& renderer,
                                              const FrameContext& ctx,
                                              const RendererHandles& handles,
                                              const entt::registry& reg,
                                              uint32_t /*w*/, uint32_t /*h*/)
{
    SA_PROFILE_SCOPE_N("Shadow::AddPasses");
    if (!m_type) return;

    AttachmentKey shadowKey{};
    shadowKey.colorCount  = 0;
    shadowKey.depthFormat = RHI::RHIFormat::D32F;

    const RHI::RHIPipelineHandle pipeline = m_type->GetOrCreatePipeline(ctx.device, shadowKey);
    if (!pipeline.IsValid()) return;

    RHI::RHIPipelineHandle skinnedShadowPipeline{};
    if (m_skinnedShadowProgram.IsLoaded())
        skinnedShadowPipeline = m_skinnedShadowProgram.GetOrCreatePipeline(ctx.device, shadowKey,
                                    RHI::RHICullMode::Front);

    const RHI::RHIDescSetHandle frameSet    = ctx.frameSet;
    const RGTextureHandle       rgShadowMap = handles.shadowMap;
    constexpr uint32_t          kSize       = 2048;

    ctx.rg->AddPass("Shadow",
        [rgShadowMap](RGPassBuilder& b) { b.WriteDepth(rgShadowMap); },
        [&drawItems = renderer.m_drawItems, &reg, pipeline, skinnedShadowPipeline,
         frameSet, rgShadowMap]
        (RHI::IRHICommandList& cmd, const RGResources& res)
        {
            SA_PROFILE_SCOPE_N("Shadow::Execute");
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

            // frameSet bind is deferred until after the first SetPipeline so
            // vkCmdBindDescriptorSets uses this pass's pipeline layout, not
            // whatever was bound previously (stale layouts cause set-0 / push-
            // constant compatibility errors).
            RHI::RHIPipelineHandle currentPipeline{};
            RHI::RHIBufferHandle   currentVB{};
            RHI::RHIBufferHandle   currentIB{};
            bool frameSetBound = false;
            for (const auto& item : drawItems) {
                if (!item.pipeline.IsValid()) continue;
                const RHI::RHIPipelineHandle effectivePipeline =
                    (item.isSkinned && skinnedShadowPipeline.IsValid())
                        ? skinnedShadowPipeline : pipeline;

                const auto* wt = reg.try_get<WorldTransformComponent>(item.entity);
                if (!wt) continue;
                const glm::mat4 world = wt->matrix * item.subLocalTransform;

                if (effectivePipeline.index != currentPipeline.index) {
                    cmd.SetPipeline(effectivePipeline);
                    currentPipeline = effectivePipeline;
                    if (!frameSetBound) {
                        cmd.SetDescriptorSet(1, frameSet);
                        frameSetBound = true;
                    }
                }
                if (item.isSkinned && item.skinDescSet.IsValid())
                    cmd.SetDescriptorSet(3, item.skinDescSet);  // Step 6.5: skin at set=3
                if (item.vertexBuffer.index != currentVB.index) {
                    cmd.SetVertexBuffer(0, item.vertexBuffer);
                    currentVB = item.vertexBuffer;
                }
                if (item.indexBuffer.index != currentIB.index) {
                    cmd.SetIndexBuffer(item.indexBuffer);
                    currentIB = item.indexBuffer;
                }
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
            cmd.SetDescriptorSet(1, descSet);  // Skybox: descSet is frameSet alias (Step 6.5)
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

    // GPU skinning: load the skinned geometry variant (skinned vert + same frag).
    if (ctx.matMgr->LoadShaderProgram(m_skinnedProgram,
                                       "deferred_geometry_skinned", "deferred_geometry", ctx))
        m_skinDescLayout = m_skinnedProgram.GetSet3Layout();  // Step 6.5: skin at set=3
    else
        SA_LOG_WARN("GBufferFeature: skinned shader not found — GPU skinning unavailable");

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
    SA_PROFILE_SCOPE_N("GBuffer::AddPasses");
    const RGTextureHandle rgRT0   = handles.gbufferRT0;
    const RGTextureHandle rgRT1   = handles.gbufferRT1;
    const RGTextureHandle rgRT2   = handles.gbufferRT2;
    const RGTextureHandle rgDepth = handles.depth;
    const RHI::RHIDescSetHandle frameSet = ctx.frameSet;
    const entt::registry* regPtr = &reg;

    // Pre-build skinned pipeline (same attachment formats, same state as PBR).
    AttachmentKey gbKey{};
    gbKey.colorCount      = 3;
    gbKey.colorFormats[0] = RHI::RHIFormat::RGBA8_UNORM;
    gbKey.colorFormats[1] = RHI::RHIFormat::RGBA16F;
    gbKey.colorFormats[2] = RHI::RHIFormat::RGBA16F;
    gbKey.depthFormat     = RHI::RHIFormat::D32F;

    RHI::RHIPipelineHandle skinnedPipeline{};
    if (m_skinnedProgram.IsLoaded())
        skinnedPipeline = m_skinnedProgram.GetOrCreatePipeline(ctx.device, gbKey);

    ctx.rg->AddPass("GBuffer",
        [rgRT0, rgRT1, rgRT2, rgDepth](RGPassBuilder& b) {
            b.Write(rgRT0);
            b.Write(rgRT1);
            b.Write(rgRT2);
            b.WriteDepth(rgDepth);
        },
        [&drawItems = renderer.m_visibleDrawItems, regPtr, frameSet, skinnedPipeline, w, h,
         rgRT0, rgRT1, rgRT2, rgDepth,
         bindlessSet = renderer.m_matMgr->GetTextureHeap().GetDescSet()]
        (RHI::IRHICommandList& cmd, const RGResources& res)
        {
            SA_PROFILE_SCOPE_N("GBuffer::Execute");
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

            // set=0 (frame uniforms) never changes within a pass — bind once,
            // but only after the first SetPipeline so vkCmdBindDescriptorSets
            // references this pass's pipeline layout.
            RHI::RHIPipelineHandle currentPipeline{};
            RHI::RHIBufferHandle   currentVB{};
            RHI::RHIBufferHandle   currentIB{};
            bool frameSetBound = false;

            for (const DrawItem* itemPtr : drawItems) {
                const auto& item = *itemPtr;

                const RHI::RHIPipelineHandle effectivePipeline =
                    (item.isSkinned && skinnedPipeline.IsValid()) ? skinnedPipeline : item.pipeline;
                if (!effectivePipeline.IsValid()) continue;

                const auto* wt = regPtr->try_get<WorldTransformComponent>(item.entity);
                if (!wt) continue;
                const glm::mat4 world = wt->matrix * item.subLocalTransform;

                const bool usesSSBO = item.material->GetType()->usesMaterialParamsSSBO;
                if (effectivePipeline.index != currentPipeline.index) {
                    cmd.SetPipeline(effectivePipeline);
                    currentPipeline = effectivePipeline;
                    // Issue #72 Step 6.5: set=0 (bindless) and set=1 (frame)
                    // share engine-wide layouts → bindings survive all pipeline
                    // changes within this pass. Bind once.
                    if (!frameSetBound) {
                        if (bindlessSet.IsValid())
                            cmd.SetDescriptorSet(0, bindlessSet);
                        cmd.SetDescriptorSet(1, frameSet);
                        frameSetBound = true;
                    }
                }
                if (usesSSBO) {
                    cmd.SetDescriptorSet(2, item.material->GetDescSet(),
                                         std::span<const uint32_t>{&item.materialUboOffset, 1});
                } else {
                    item.material->Bind(&cmd);
                }
                if (item.isSkinned && item.skinDescSet.IsValid())
                    cmd.SetDescriptorSet(3, item.skinDescSet);
                if (item.vertexBuffer.index != currentVB.index) {
                    cmd.SetVertexBuffer(0, item.vertexBuffer);
                    currentVB = item.vertexBuffer;
                }
                if (item.indexBuffer.index != currentIB.index) {
                    cmd.SetIndexBuffer(item.indexBuffer);
                    currentIB = item.indexBuffer;
                }
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

void SceneRenderer::DeferredLightingFeature::AddPasses([[maybe_unused]] SceneRenderer& renderer,
                                                        const FrameContext& ctx,
                                                        const RendererHandles& handles,
                                                        const entt::registry& /*reg*/,
                                                        uint32_t w, uint32_t h)
{
    SA_PROFILE_SCOPE_N("DeferredLighting::AddPasses");
    if (!m_type || !m_gbDescSet.IsValid()) return;

    ctx.BindTexture(m_gbDescSet, 0, handles.gbufferRT0);
    ctx.BindTexture(m_gbDescSet, 1, handles.gbufferRT1);
    ctx.BindTexture(m_gbDescSet, 2, handles.gbufferRT2);
    ctx.BindTexture(m_gbDescSet, 3, handles.depth);
    ctx.BindTexture(m_gbDescSet, 4, handles.shadowMap);
    ctx.BindTexture(m_gbDescSet, 5, handles.ssaoTex);

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
    const RGTextureHandle rgSsaoTex  = handles.ssaoTex;

    ctx.rg->AddPass("DeferredLighting",
        [rgRT0, rgRT1, rgRT2, rgDepth, rgHdr, rgShadowMap, rgSsaoTex](RGPassBuilder& b) {
            b.Read(rgRT0);
            b.Read(rgRT1);
            b.Read(rgRT2);
            b.Read(rgDepth);
            b.Read(rgShadowMap);  // triggers layout transition DEPTH_ATTACHMENT → SHADER_READ_ONLY
            b.Read(rgSsaoTex);
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
            cmd.SetDescriptorSet(1, frameSet);
            cmd.SetDescriptorSet(2, gbDescSet);
            cmd.Draw(3, 1, 0, 0);
            cmd.EndRenderPass();
        });
}

void SceneRenderer::DeferredLightingFeature::ReloadShaders(
    RHI::IRHIDevice*             device,
    std::span<const uint8_t>     fragSpv,
    const RHI::ShaderReflection& fragRefl)
{
    if (!m_type) return;
    if (!m_type->shader.ReloadFragShader(device, fragSpv, fragRefl))
        SA_LOG_ERROR("DeferredLightingFeature: frag shader reload failed");
    else
        SA_LOG_INFO("DeferredLightingFeature: dispatch hot-swapped");
}

// ── SceneRenderer::ApplyProjectShaderTypes ────────────────────────────────────

void SceneRenderer::ApplyProjectShaderTypes(const std::string& cookedShaderDir) {
    // Remove old project-specific MaterialTypes (instances already cleared by caller).
    m_matMgr->ClearProjectTypes();

    if (cookedShaderDir.empty()) return;

    namespace fs = std::filesystem;

    // Hot-swap deferred_lighting.frag when a project-specific SPV was produced.
    const std::string newSpvPath = cookedShaderDir + "/deferred_lighting.frag.spv";
    if (m_deferredLightingFeature && fs::exists(newSpvPath)) {
        auto fragSpv = LoadComputeSpv(newSpvPath);

        // Reuse the original .refl — binding layout does not change between dispatches.
        const std::string reflPath = m_shaderDir + "/deferred_lighting.frag.refl";
        RHI::ShaderReflection fragRefl;
        RHI::ShaderReflectionIO::LoadFromFile(reflPath, fragRefl);

        if (!fragSpv.empty())
            m_deferredLightingFeature->ReloadShaders(m_device, fragSpv, fragRefl);
    }

    // Register new project material types. Project shaders may reference vert/frag
    // SPV (e.g. deferred_geometry.vert) that lives in the engine dir, so we pass
    // m_shaderDir as the fallback.
    FeatureInitContext ctx;
    ctx.device          = m_device;
    ctx.matMgr          = m_matMgr;
    ctx.resMgr          = m_resMgr;
    ctx.frameLayout     = m_frameUniforms.GetLayout();
    ctx.shaderDir       = cookedShaderDir;
    ctx.engineShaderDir = m_shaderDir;
    m_matMgr->RegisterTypesFromShaderDir(cookedShaderDir, ctx, /*isProjectType=*/true);

    SA_LOG_INFO("SceneRenderer: project shader types applied from '{}'", cookedShaderDir);
}

// ── SSAOFeature ───────────────────────────────────────────────────────────────

void SceneRenderer::SSAOFeature::OnInit(const FeatureInitContext& ctx)
{
    ctx.matMgr->RegisterTypeFromShaders(
        {"SSAO", "fullscreen_tri", "ssao",
         RHI::RHICullMode::None, RHI::RHIBlendMode::Opaque, RHI::RHITopology::TriangleList, false, false, true}, ctx);
    ctx.matMgr->RegisterTypeFromShaders(
        {"SSAOBlur", "fullscreen_tri", "ssao_blur",
         RHI::RHICullMode::None, RHI::RHIBlendMode::Opaque, RHI::RHITopology::TriangleList, false, false, true}, ctx);

    m_gtaoType = ctx.matMgr->GetType("SSAO");
    m_blurType = ctx.matMgr->GetType("SSAOBlur");
    if (!m_gtaoType || !m_blurType) {
        SA_LOG_WARN("SSAOFeature: shader load failed — SSAO disabled");
        return;
    }

    m_gtaoDescSet  = ctx.device->AllocateDescriptorSet(m_gtaoType->shader.GetMaterialLayout());
    m_blurHDescSet = ctx.device->AllocateDescriptorSet(m_blurType->shader.GetMaterialLayout());
    m_blurVDescSet = ctx.device->AllocateDescriptorSet(m_blurType->shader.GetMaterialLayout());
}

void SceneRenderer::SSAOFeature::OnShutdown(RHI::IRHIDevice* /*device*/)
{
    m_gtaoDescSet  = {};
    m_blurHDescSet = {};
    m_blurVDescSet = {};
    m_gtaoType     = nullptr;
    m_blurType     = nullptr;
}

void SceneRenderer::SSAOFeature::AddPasses(SceneRenderer& /*renderer*/,
                                            const FrameContext& ctx,
                                            const RendererHandles& handles,
                                            const entt::registry& /*reg*/,
                                            uint32_t w, uint32_t h)
{
    SA_PROFILE_SCOPE_N("SSAO::AddPasses");
    if (!m_gtaoType || !m_gtaoDescSet.IsValid()) return;

    // All three SSAO passes run at half resolution; ssaoTex is also half-res.
    // DeferredLighting bilinear-upsamples the result — AO is low-frequency, loss is imperceptible.
    const uint32_t hw = std::max(1u, w / 2);
    const uint32_t hh = std::max(1u, h / 2);

    // GTAO reads depth + gbufferRT1 — both are imported every frame, rebind unconditionally.
    ctx.BindTexture(m_gtaoDescSet, 0, handles.depth);
    ctx.BindTexture(m_gtaoDescSet, 1, handles.gbufferRT1);
    // Blur passes: binding=1 (depth) is imported — bind now.
    // binding=0 (AO input) is a transient — written inside execute lambdas via res.Get().
    ctx.BindTexture(m_blurHDescSet, 1, handles.depth);
    ctx.BindTexture(m_blurVDescSet, 1, handles.depth);

    // ── Create transient intermediates (aliased by the RG) ───────────────────
    RHI::RHITextureDesc r8Desc{};
    r8Desc.width     = hw;
    r8Desc.height    = hh;
    r8Desc.format    = RHI::RHIFormat::R8_UNORM;
    r8Desc.usage     = RHI::RHITextureUsage::RenderTarget | RHI::RHITextureUsage::Sampled;
    r8Desc.debugName = "AO_Raw";
    const RGTextureHandle rgRawAO = ctx.rg->CreateTexture("AO_Raw",   r8Desc);
    r8Desc.debugName = "AO_BlurH";
    const RGTextureHandle rgBlurH = ctx.rg->CreateTexture("AO_BlurH", r8Desc);

    // Transient bindings — resolved by FlushBindings() after AllocateSlots().
    ctx.BindTexture(m_blurHDescSet, 0, rgRawAO);
    ctx.BindTexture(m_blurVDescSet, 0, rgBlurH);

    const RGTextureHandle rgDepth  = handles.depth;
    const RGTextureHandle rgNormal = handles.gbufferRT1;
    const RGTextureHandle rgSsao   = handles.ssaoTex;

    // ── Attachment key for R8 single-channel output ───────────────────────────
    AttachmentKey r8Key{};
    r8Key.colorCount      = 1;
    r8Key.colorFormats[0] = RHI::RHIFormat::R8_UNORM;
    r8Key.depthFormat     = RHI::RHIFormat::Undefined;

    const RHI::RHIPipelineHandle gtaoPipeline = m_gtaoType->GetOrCreatePipeline(ctx.device, r8Key);
    const RHI::RHIPipelineHandle blurPipeline = m_blurType->GetOrCreatePipeline(ctx.device, r8Key);

    // ── Pass 1: GTAO (or disabled fill: push enabled=0.0 → writes 1.0) ───────
    struct GTAOPC {
        float enabled;
        float radius;
        float strength;
        float bias;
        int   directions;
        int   steps;
        float _pad0;
        float _pad1;
    };
    const GTAOPC gtaoPC{
        m_enabled ? 1.f : 0.f,
        m_radius, m_strength, m_bias,
        m_directions, m_steps, 0.f, 0.f
    };

    {
        const RHI::RHIDescSetHandle gtaoDescSet = m_gtaoDescSet;
        const RHI::RHIDescSetHandle frameSet    = ctx.frameSet;
        ctx.rg->AddPass("GTAO",
            [rgDepth, rgNormal, rgRawAO](RGPassBuilder& b) {
                b.Read(rgDepth);
                b.Read(rgNormal);
                b.Write(rgRawAO);
            },
            [gtaoPipeline, frameSet, gtaoDescSet, gtaoPC, rgRawAO, hw, hh]
            (RHI::IRHICommandList& cmd, const RGResources& res)
            {
                RHI::RHIRenderPassDesc rp{};
                rp.colorAttachmentCount            = 1;
                rp.colorAttachments[0].texture     = res.Get(rgRawAO);
                rp.colorAttachments[0].clearOnLoad = true;
                rp.width  = hw;
                rp.height = hh;
                cmd.BeginRenderPass(rp);
                cmd.SetViewport(RHI::RHIViewport{0.f, 0.f, float(hw), float(hh)});
                cmd.SetScissor(RHI::RHIScissor{0, 0, hw, hh});
                cmd.SetPipeline(gtaoPipeline);
                cmd.SetDescriptorSet(1, frameSet);
                cmd.SetDescriptorSet(2, gtaoDescSet);
                cmd.SetPushConstants(&gtaoPC, sizeof(gtaoPC), RHI::RHIShaderStage::Fragment);
                cmd.Draw(3, 1, 0, 0);
                cmd.EndRenderPass();
            });
    }

    // ── Pass 2: bilateral blur H ──────────────────────────────────────────────
    {
        const RHI::RHIDescSetHandle blurHDescSet = m_blurHDescSet;
        const RHI::RHIDescSetHandle frameSet     = ctx.frameSet;
        struct BlurPC { float sx; float sy; float sharpness; float _pad; };
        const BlurPC blurHPC{1.f / float(hw), 0.f, m_blurSharpness, 0.f};

        ctx.rg->AddPass("SSAO_BlurH",
            [rgRawAO, rgDepth, rgBlurH](RGPassBuilder& b) {
                b.Read(rgRawAO);
                b.Read(rgDepth);
                b.Write(rgBlurH);
            },
            [blurPipeline, frameSet, blurHDescSet, blurHPC, rgBlurH, hw, hh]
            (RHI::IRHICommandList& cmd, const RGResources& res)
            {
                RHI::RHIRenderPassDesc rp{};
                rp.colorAttachmentCount            = 1;
                rp.colorAttachments[0].texture     = res.Get(rgBlurH);
                rp.colorAttachments[0].clearOnLoad = true;
                rp.width  = hw;
                rp.height = hh;
                cmd.BeginRenderPass(rp);
                cmd.SetViewport(RHI::RHIViewport{0.f, 0.f, float(hw), float(hh)});
                cmd.SetScissor(RHI::RHIScissor{0, 0, hw, hh});
                cmd.SetPipeline(blurPipeline);
                cmd.SetDescriptorSet(1, frameSet);
                cmd.SetDescriptorSet(2, blurHDescSet);
                cmd.SetPushConstants(&blurHPC, sizeof(blurHPC), RHI::RHIShaderStage::Fragment);
                cmd.Draw(3, 1, 0, 0);
                cmd.EndRenderPass();
            });
    }

    // ── Pass 3: bilateral blur V → ssaoTex (half-res, imported in RendererHandles) ──
    {
        const RHI::RHIDescSetHandle blurVDescSet = m_blurVDescSet;
        const RHI::RHIDescSetHandle frameSet     = ctx.frameSet;
        struct BlurPC { float sx; float sy; float sharpness; float _pad; };
        const BlurPC blurVPC{0.f, 1.f / float(hh), m_blurSharpness, 0.f};

        ctx.rg->AddPass("SSAO_BlurV",
            [rgBlurH, rgDepth, rgSsao](RGPassBuilder& b) {
                b.Read(rgBlurH);
                b.Read(rgDepth);
                b.Write(rgSsao);
            },
            [blurPipeline, frameSet, blurVDescSet, blurVPC, rgSsao, hw, hh]
            (RHI::IRHICommandList& cmd, const RGResources& res)
            {
                RHI::RHIRenderPassDesc rp{};
                rp.colorAttachmentCount            = 1;
                rp.colorAttachments[0].texture     = res.Get(rgSsao);
                rp.colorAttachments[0].clearOnLoad = true;
                rp.width  = hw;
                rp.height = hh;
                cmd.BeginRenderPass(rp);
                cmd.SetViewport(RHI::RHIViewport{0.f, 0.f, float(hw), float(hh)});
                cmd.SetScissor(RHI::RHIScissor{0, 0, hw, hh});
                cmd.SetPipeline(blurPipeline);
                cmd.SetDescriptorSet(1, frameSet);
                cmd.SetDescriptorSet(2, blurVDescSet);
                cmd.SetPushConstants(&blurVPC, sizeof(blurVPC), RHI::RHIShaderStage::Fragment);
                cmd.Draw(3, 1, 0, 0);
                cmd.EndRenderPass();
            });
    }
}

// ── TAAFeature ────────────────────────────────────────────────────────────────

void SceneRenderer::TAAFeature::OnInit(const FeatureInitContext& ctx)
{
    ctx.matMgr->RegisterTypeFromShaders(
        {"TAA", "fullscreen_tri", "taa_resolve",
         RHI::RHICullMode::None, RHI::RHIBlendMode::Opaque, RHI::RHITopology::TriangleList, false, false, true}, ctx);

    m_taaType = ctx.matMgr->GetType("TAA");
    if (!m_taaType) {
        SA_LOG_WARN("TAAFeature: shader load failed — TAA disabled");
        return;
    }
    m_resolveSet = ctx.device->AllocateDescriptorSet(m_taaType->shader.GetMaterialLayout());
}

void SceneRenderer::TAAFeature::OnShutdown(RHI::IRHIDevice* device)
{
    m_resolveSet = {};
    m_taaType    = nullptr;
    for (int i = 0; i < 2; ++i) {
        if (m_historyTex[i].IsValid()) device->DestroyTexture(m_historyTex[i]);
        m_historyTex[i] = {};
    }
}

void SceneRenderer::TAAFeature::AddPasses(SceneRenderer& /*renderer*/,
                                           const FrameContext& ctx,
                                           const RendererHandles& handles,
                                           const entt::registry& /*reg*/,
                                           uint32_t w, uint32_t h)
{
    SA_PROFILE_SCOPE_N("TAA::AddPasses");
    if (!m_enabled || !m_taaType || !m_resolveSet.IsValid()) {
        m_outputHandle = {};  // invalid → feature loop leaves handles.taaResolved = handles.hdr
        return;
    }

    // ── Resize / first init ───────────────────────────────────────────────────
    if (w != m_trackedW || h != m_trackedH) {
        for (int i = 0; i < 2; ++i) {
            if (m_historyTex[i].IsValid()) ctx.device->DestroyTexture(m_historyTex[i]);
            RHI::RHITextureDesc d{};
            d.width     = w;
            d.height    = h;
            d.format    = RHI::RHIFormat::RGBA16F;
            d.usage     = RHI::RHITextureUsage::RenderTarget | RHI::RHITextureUsage::Sampled;
            d.debugName = (i == 0) ? "TAA_History0" : "TAA_History1";
            m_historyTex[i] = ctx.device->CreateTexture(d);
        }
        m_trackedW     = w;
        m_trackedH     = h;
        m_historyValid = false;
    }

    const int prevIndex = m_historyIndex;
    const int currIndex = 1 - prevIndex;

    // Import ping-pong textures: prevTex is read, currTex is written (becomes new history).
    const RGTextureHandle rgHistoryRead = ctx.rg->ImportTexture("TAA_HistoryRead",
        m_historyTex[prevIndex],
        RHI::RHIResourceState::Undefined, RHI::RHIResourceState::Undefined);
    const RGTextureHandle rgHistoryWrite = ctx.rg->ImportTexture("TAA_HistoryWrite",
        m_historyTex[currIndex],
        RHI::RHIResourceState::Undefined, RHI::RHIResourceState::Undefined);

    ctx.BindTexture(m_resolveSet, 0, handles.hdr);
    ctx.BindTexture(m_resolveSet, 1, rgHistoryRead);
    ctx.BindTexture(m_resolveSet, 2, handles.depth);

    AttachmentKey hdrKey{};
    hdrKey.colorCount      = 1;
    hdrKey.colorFormats[0] = RHI::RHIFormat::RGBA16F;
    hdrKey.depthFormat     = RHI::RHIFormat::Undefined;
    const RHI::RHIPipelineHandle taaPipeline = m_taaType->GetOrCreatePipeline(ctx.device, hdrKey);

    struct TAAPC {
        float blendStatic;
        float blendMotion;
        float historyValid;
        float antiGhosting;
    };
    const TAAPC taaPC{ m_blendStatic, m_blendMotion, m_historyValid ? 1.f : 0.f, m_antiGhosting ? 1.f : 0.f };

    const RGTextureHandle rgCurrent  = handles.hdr;
    const RGTextureHandle rgDepth    = handles.depth;
    const RHI::RHIDescSetHandle resolveSet = m_resolveSet;
    const RHI::RHIDescSetHandle frameSet   = ctx.frameSet;

    ctx.rg->AddPass("TAA_Resolve",
        [rgCurrent, rgDepth, rgHistoryRead, rgHistoryWrite](RGPassBuilder& b) {
            b.Read(rgCurrent);
            b.Read(rgDepth);
            b.Read(rgHistoryRead);
            b.Write(rgHistoryWrite);
        },
        [taaPipeline, frameSet, resolveSet, taaPC, rgHistoryWrite, w, h]
        (RHI::IRHICommandList& cmd, const RGResources& res)
        {
            RHI::RHIRenderPassDesc rp{};
            rp.colorAttachmentCount            = 1;
            rp.colorAttachments[0].texture     = res.Get(rgHistoryWrite);
            rp.colorAttachments[0].clearOnLoad = false;
            rp.width  = w;
            rp.height = h;
            cmd.BeginRenderPass(rp);
            cmd.SetViewport(RHI::RHIViewport{0.f, 0.f, float(w), float(h)});
            cmd.SetScissor(RHI::RHIScissor{0, 0, w, h});
            cmd.SetPipeline(taaPipeline);
            cmd.SetDescriptorSet(1, frameSet);
            cmd.SetDescriptorSet(2, resolveSet);
            cmd.SetPushConstants(&taaPC, sizeof(taaPC), RHI::RHIShaderStage::Fragment);
            cmd.Draw(3, 1, 0, 0);
            cmd.EndRenderPass();
        });

    m_outputHandle = rgHistoryWrite;
    m_historyIndex = currIndex;
    m_historyValid = true;
}

// ── DoFFeature ────────────────────────────────────────────────────────────────

void SceneRenderer::DoFFeature::OnInit(const FeatureInitContext& ctx)
{
    ctx.matMgr->RegisterTypeFromShaders(
        {"DoF_CoC", "fullscreen_tri", "dof_coc",
         RHI::RHICullMode::None, RHI::RHIBlendMode::Opaque, RHI::RHITopology::TriangleList, false, false, true}, ctx);
    ctx.matMgr->RegisterTypeFromShaders(
        {"DoF_Blur", "fullscreen_tri", "dof_blur",
         RHI::RHICullMode::None, RHI::RHIBlendMode::Opaque, RHI::RHITopology::TriangleList, false, false, true}, ctx);
    ctx.matMgr->RegisterTypeFromShaders(
        {"DoF_Composite", "fullscreen_tri", "dof_composite",
         RHI::RHICullMode::None, RHI::RHIBlendMode::Opaque, RHI::RHITopology::TriangleList, false, false, true}, ctx);

    m_cocMat       = ctx.matMgr->GetType("DoF_CoC");
    m_blurMat      = ctx.matMgr->GetType("DoF_Blur");
    m_compositeMat = ctx.matMgr->GetType("DoF_Composite");
    if (!m_cocMat || !m_blurMat || !m_compositeMat) {
        SA_LOG_WARN("DoFFeature: shader load failed — DoF disabled");
        return;
    }

    m_cocDescSet      = ctx.device->AllocateDescriptorSet(m_cocMat->shader.GetMaterialLayout());
    for (int i = 0; i < 4; ++i)
        m_blurDescSets[i] = ctx.device->AllocateDescriptorSet(m_blurMat->shader.GetMaterialLayout());
    m_compositeDescSet = ctx.device->AllocateDescriptorSet(m_compositeMat->shader.GetMaterialLayout());
}

void SceneRenderer::DoFFeature::OnShutdown(RHI::IRHIDevice* /*device*/)
{
    m_cocDescSet = {};
    for (int i = 0; i < 4; ++i) m_blurDescSets[i] = {};
    m_compositeDescSet = {};
    m_cocMat = m_blurMat = m_compositeMat = nullptr;
}

void SceneRenderer::DoFFeature::AddPasses(SceneRenderer& /*renderer*/,
                                           const FrameContext& ctx,
                                           const RendererHandles& handles,
                                           const entt::registry& /*reg*/,
                                           uint32_t w, uint32_t h)
{
    SA_PROFILE_SCOPE_N("DoF::AddPasses");
    if (!m_enabled || !m_cocMat || !m_cocDescSet.IsValid()) {
        m_outputHandle = {};
        return;
    }

    // ── Transient intermediates ───────────────────────────────────────────────
    RHI::RHITextureDesc r32fDesc{};
    r32fDesc.width     = w;
    r32fDesc.height    = h;
    r32fDesc.format    = RHI::RHIFormat::R32F;
    r32fDesc.usage     = RHI::RHITextureUsage::RenderTarget | RHI::RHITextureUsage::Sampled;
    r32fDesc.debugName = "DoF_CoC";
    const RGTextureHandle rgCoc = ctx.rg->CreateTexture("DoF_CoC", r32fDesc);

    RHI::RHITextureDesc rgbaDesc{};
    rgbaDesc.width     = w;
    rgbaDesc.height    = h;
    rgbaDesc.format    = RHI::RHIFormat::RGBA16F;
    rgbaDesc.usage     = RHI::RHITextureUsage::RenderTarget | RHI::RHITextureUsage::Sampled;
    rgbaDesc.debugName = "DoF_NearH";
    const RGTextureHandle rgNearH   = ctx.rg->CreateTexture("DoF_NearH",  rgbaDesc);
    rgbaDesc.debugName = "DoF_Near";
    const RGTextureHandle rgNear    = ctx.rg->CreateTexture("DoF_Near",   rgbaDesc);
    rgbaDesc.debugName = "DoF_FarH";
    const RGTextureHandle rgFarH    = ctx.rg->CreateTexture("DoF_FarH",   rgbaDesc);
    rgbaDesc.debugName = "DoF_Far";
    const RGTextureHandle rgFar     = ctx.rg->CreateTexture("DoF_Far",    rgbaDesc);
    rgbaDesc.debugName = "DoF_Output";
    const RGTextureHandle rgOutput  = ctx.rg->CreateTexture("DoF_Output", rgbaDesc);
    m_outputHandle = rgOutput;

    // ── Descriptor bindings (transient handles resolved by FlushBindings) ─────
    ctx.BindTexture(m_cocDescSet, 0, handles.depth);

    // Blur near-H: src=hdr, V: src=nearH; far-H: src=hdr, V: src=farH
    ctx.BindTexture(m_blurDescSets[0], 0, handles.hdr);  ctx.BindTexture(m_blurDescSets[0], 1, rgCoc);
    ctx.BindTexture(m_blurDescSets[1], 0, rgNearH);      ctx.BindTexture(m_blurDescSets[1], 1, rgCoc);
    ctx.BindTexture(m_blurDescSets[2], 0, handles.hdr);  ctx.BindTexture(m_blurDescSets[2], 1, rgCoc);
    ctx.BindTexture(m_blurDescSets[3], 0, rgFarH);       ctx.BindTexture(m_blurDescSets[3], 1, rgCoc);

    ctx.BindTexture(m_compositeDescSet, 0, handles.hdr);
    ctx.BindTexture(m_compositeDescSet, 1, rgCoc);
    ctx.BindTexture(m_compositeDescSet, 2, rgNear);
    ctx.BindTexture(m_compositeDescSet, 3, rgFar);

    // ── Attachment keys ───────────────────────────────────────────────────────
    AttachmentKey cocKey{};
    cocKey.colorCount      = 1;
    cocKey.colorFormats[0] = RHI::RHIFormat::R32F;
    cocKey.depthFormat     = RHI::RHIFormat::Undefined;

    AttachmentKey hdrKey{};
    hdrKey.colorCount      = 1;
    hdrKey.colorFormats[0] = RHI::RHIFormat::RGBA16F;
    hdrKey.depthFormat     = RHI::RHIFormat::Undefined;

    const RHI::RHIPipelineHandle cocPipeline       = m_cocMat->GetOrCreatePipeline(ctx.device, cocKey);
    const RHI::RHIPipelineHandle blurPipeline      = m_blurMat->GetOrCreatePipeline(ctx.device, hdrKey);
    const RHI::RHIPipelineHandle compositePipeline = m_compositeMat->GetOrCreatePipeline(ctx.device, hdrKey);

    const RHI::RHIDescSetHandle frameSet       = ctx.frameSet;
    const RHI::RHIDescSetHandle cocDs          = m_cocDescSet;
    const RHI::RHIDescSetHandle blurDs0        = m_blurDescSets[0];
    const RHI::RHIDescSetHandle blurDs1        = m_blurDescSets[1];
    const RHI::RHIDescSetHandle blurDs2        = m_blurDescSets[2];
    const RHI::RHIDescSetHandle blurDs3        = m_blurDescSets[3];
    const RHI::RHIDescSetHandle compositeDs    = m_compositeDescSet;

    const RGTextureHandle rgHdr   = handles.hdr;
    const RGTextureHandle rgDepth = handles.depth;

    // Push constant values captured per-pass
    struct DofCocPC  { float focusDist; float aperture; float focalLength; float maxCocPx; };
    struct DofBlurPC { int isHorizontal; int isNear; float maxCocPx; int samples; };
    struct DofCompPC { float maxCocPx; float nearTransition; float farTransition; float _pad; };

    const DofCocPC  cocPC { m_focusDist, m_aperture, m_focalLength, m_maxCocPx };
    const float nearTrans = m_maxCocPx * 0.25f;  // blend start at 25% of max CoC
    const float farTrans  = m_maxCocPx * 0.25f;
    const DofCompPC compPC{ m_maxCocPx, nearTrans, farTrans, 0.f };

    // ── Pass 1: CoC ────────────────────────────────────────────────────────────
    {
        ctx.rg->AddPass("DoF_CoC",
            [rgDepth, rgCoc](RGPassBuilder& b) { b.Read(rgDepth); b.Write(rgCoc); },
            [cocPipeline, frameSet, cocDs, cocPC, rgCoc, w, h]
            (RHI::IRHICommandList& cmd, const RGResources& res)
            {
                RHI::RHIRenderPassDesc rp{};
                rp.colorAttachmentCount            = 1;
                rp.colorAttachments[0].texture     = res.Get(rgCoc);
                rp.colorAttachments[0].clearOnLoad = true;
                rp.width = w; rp.height = h;
                cmd.BeginRenderPass(rp);
                cmd.SetViewport(RHI::RHIViewport{0.f, 0.f, float(w), float(h)});
                cmd.SetScissor(RHI::RHIScissor{0, 0, w, h});
                cmd.SetPipeline(cocPipeline);
                cmd.SetDescriptorSet(1, frameSet);
                cmd.SetDescriptorSet(2, cocDs);
                cmd.SetPushConstants(&cocPC, sizeof(cocPC), RHI::RHIShaderStage::Fragment);
                cmd.Draw(3, 1, 0, 0);
                cmd.EndRenderPass();
            });
    }

    // ── Passes 2–5: Blur (near H, near V, far H, far V) ──────────────────────
    auto addBlurPass = [&](const char* name, int isH, int isN,
                            const RHI::RHIDescSetHandle& ds,
                            RGTextureHandle rgSrc, RGTextureHandle rgDst)
    {
        const DofBlurPC blurPC{ isH, isN, m_maxCocPx, m_samples };
        ctx.rg->AddPass(name,
            [rgSrc, rgCoc, rgDst](RGPassBuilder& b) {
                b.Read(rgSrc); b.Read(rgCoc); b.Write(rgDst);
            },
            [blurPipeline, frameSet, ds, blurPC, rgDst, w, h]
            (RHI::IRHICommandList& cmd, const RGResources& res)
            {
                RHI::RHIRenderPassDesc rp{};
                rp.colorAttachmentCount            = 1;
                rp.colorAttachments[0].texture     = res.Get(rgDst);
                rp.colorAttachments[0].clearOnLoad = true;
                rp.width = w; rp.height = h;
                cmd.BeginRenderPass(rp);
                cmd.SetViewport(RHI::RHIViewport{0.f, 0.f, float(w), float(h)});
                cmd.SetScissor(RHI::RHIScissor{0, 0, w, h});
                cmd.SetPipeline(blurPipeline);
                cmd.SetDescriptorSet(1, frameSet);
                cmd.SetDescriptorSet(2, ds);
                cmd.SetPushConstants(&blurPC, sizeof(blurPC), RHI::RHIShaderStage::Fragment);
                cmd.Draw(3, 1, 0, 0);
                cmd.EndRenderPass();
            });
    };

    addBlurPass("DoF_NearH", 1, 1, blurDs0, rgHdr,   rgNearH);
    addBlurPass("DoF_NearV", 0, 1, blurDs1, rgNearH, rgNear);
    addBlurPass("DoF_FarH",  1, 0, blurDs2, rgHdr,   rgFarH);
    addBlurPass("DoF_FarV",  0, 0, blurDs3, rgFarH,  rgFar);

    // ── Pass 6: Composite ─────────────────────────────────────────────────────
    {
        ctx.rg->AddPass("DoF_Composite",
            [rgHdr, rgCoc, rgNear, rgFar, rgOutput](RGPassBuilder& b) {
                b.Read(rgHdr); b.Read(rgCoc);
                b.Read(rgNear); b.Read(rgFar);
                b.Write(rgOutput);
            },
            [compositePipeline, frameSet, compositeDs, compPC, rgOutput, w, h]
            (RHI::IRHICommandList& cmd, const RGResources& res)
            {
                RHI::RHIRenderPassDesc rp{};
                rp.colorAttachmentCount            = 1;
                rp.colorAttachments[0].texture     = res.Get(rgOutput);
                rp.colorAttachments[0].clearOnLoad = true;
                rp.width = w; rp.height = h;
                cmd.BeginRenderPass(rp);
                cmd.SetViewport(RHI::RHIViewport{0.f, 0.f, float(w), float(h)});
                cmd.SetScissor(RHI::RHIScissor{0, 0, w, h});
                cmd.SetPipeline(compositePipeline);
                cmd.SetDescriptorSet(1, frameSet);
                cmd.SetDescriptorSet(2, compositeDs);
                cmd.SetPushConstants(&compPC, sizeof(compPC), RHI::RHIShaderStage::Fragment);
                cmd.Draw(3, 1, 0, 0);
                cmd.EndRenderPass();
            });
    }
}

// ── AutoExposureFeature ───────────────────────────────────────────────────────

void SceneRenderer::AutoExposureFeature::OnInit(const FeatureInitContext& ctx)
{
    const std::string histoSpvPath  = ctx.shaderDir + "/postfx_histogram.comp.spv";
    const std::string histoReflPath = ctx.shaderDir + "/postfx_histogram.comp.refl";
    const std::string adaptSpvPath  = ctx.shaderDir + "/postfx_exposure_adapt.comp.spv";
    const std::string adaptReflPath = ctx.shaderDir + "/postfx_exposure_adapt.comp.refl";

    const auto histoSpv = LoadComputeSpv(histoSpvPath);
    const auto adaptSpv = LoadComputeSpv(adaptSpvPath);
    if (histoSpv.empty() || adaptSpv.empty()) {
        SA_LOG_WARN("AutoExposureFeature: compute shader(s) not found — feature disabled");
        return;
    }

    if (!m_histoProg.Load(ctx.device, {histoSpv, LoadComputeRefl(histoReflPath)}) ||
        !m_adaptProg.Load(ctx.device, {adaptSpv, LoadComputeRefl(adaptReflPath)})) {
        SA_LOG_WARN("AutoExposureFeature: ComputeProgram::Load failed — feature disabled");
        return;
    }

    // Persistent exposure SSBO (1 × float, device-local, Storage|CopySrc)
    RHI::RHIBufferDesc expDesc{};
    expDesc.size      = sizeof(float);
    expDesc.usage     = RHI::RHIBufferUsage::Storage | RHI::RHIBufferUsage::CopySrc;
    expDesc.debugName = "AE_Exposure";
    m_exposureSsbo = ctx.device->CreateBuffer(expDesc);

    // CPU-visible staging buffer for per-frame readback (1 × float, CopyDst)
    RHI::RHIBufferDesc stgDesc{};
    stgDesc.size       = sizeof(float);
    stgDesc.usage      = RHI::RHIBufferUsage::CopyDst;
    stgDesc.cpuVisible = true;
    stgDesc.debugName  = "AE_ExposureStaging";
    m_exposureStaging = ctx.device->CreateBuffer(stgDesc);

    // Seed initial exposure value = 1.0 in both buffers so first frame is stable.
    const float initVal = 1.0f;
    ctx.device->UploadBufferData(m_exposureSsbo,    &initVal, sizeof(float));
    ctx.device->UploadBufferData(m_exposureStaging, &initVal, sizeof(float));

    // Allocate descriptor sets from per-pass set=1 layouts.
    const RHI::RHIDescLayoutHandle histoLayout = m_histoProg.GetLayout(1);
    const RHI::RHIDescLayoutHandle adaptLayout = m_adaptProg.GetLayout(1);
    if (!histoLayout.IsValid() || !adaptLayout.IsValid()) {
        SA_LOG_WARN("AutoExposureFeature: missing set=1 layouts — feature disabled");
        return;
    }
    m_histoSet = ctx.device->AllocateDescriptorSet(histoLayout);
    m_adaptSet = ctx.device->AllocateDescriptorSet(adaptLayout);

    // Pre-bind the persistent exposure SSBO to m_adaptSet binding=1.
    // (histo binding=1 and adaptSet binding=0 are transient → deferred via BindBuffer)
    ctx.device->WriteDescriptorBuffer(m_adaptSet, 1, m_exposureSsbo);
}

void SceneRenderer::AutoExposureFeature::OnShutdown(RHI::IRHIDevice* device)
{
    m_histoProg.Unload(device);
    m_adaptProg.Unload(device);
    if (m_exposureSsbo.IsValid())    device->DestroyBuffer(m_exposureSsbo);
    if (m_exposureStaging.IsValid()) device->DestroyBuffer(m_exposureStaging);
    m_exposureSsbo    = {};
    m_exposureStaging = {};
    m_histoSet = {};
    m_adaptSet = {};
}

void SceneRenderer::AutoExposureFeature::AddPasses(SceneRenderer& /*renderer*/,
                                                    const FrameContext& ctx,
                                                    const RendererHandles& handles,
                                                    const entt::registry& /*reg*/,
                                                    uint32_t w, uint32_t h)
{
    if (!m_enabled || !m_histoProg.IsLoaded() || !m_adaptProg.IsLoaded()) return;
    if (!m_histoSet.IsValid() || !m_adaptSet.IsValid()) return;

    // Per-frame delta time from steady_clock.
    const auto now = std::chrono::steady_clock::now();
    const float dt = m_timerInit
        ? std::chrono::duration<float>(now - m_lastTime).count()
        : (1.f / 60.f);
    m_lastTime  = now;
    m_timerInit = true;

    // Transient 256-bin histogram SSBO — cleared each frame by RG (clearOnCreate).
    RGBufferDesc histoDesc{};
    histoDesc.size          = 256 * sizeof(uint32_t);
    histoDesc.usage         = RHI::RHIBufferUsage::Storage | RHI::RHIBufferUsage::CopyDst;
    histoDesc.clearOnCreate = true;
    histoDesc.debugName     = "AE_Histo";
    const RGBufferHandle hHisto = ctx.rg->CreateBuffer("AE_Histo", histoDesc);

    // Import the persistent exposure SSBO so the RG tracks its state.
    const RGBufferHandle hExposure = ctx.rg->ImportBuffer(
        "AE_Exposure", m_exposureSsbo, RHI::RHIBufferState::StorageWrite);

    // Deferred texture/buffer bindings — resolved by FlushBindings after AllocateSlots.
    ctx.BindTexture(m_histoSet, 0, handles.hdr);
    ctx.BindBuffer (m_histoSet, 1, hHisto);
    ctx.BindBuffer (m_adaptSet, 0, hHisto);

    const RHI::RHIPipelineHandle histoPipeline = m_histoProg.GetPipeline(ctx.device);
    const RHI::RHIPipelineHandle adaptPipeline = m_adaptProg.GetPipeline(ctx.device);
    const RHI::RHIDescSetHandle  histoSet   = m_histoSet;
    const RHI::RHIDescSetHandle  adaptSet   = m_adaptSet;
    const RHI::RHIBufferHandle   expStaging = m_exposureStaging;

    struct HistoPC { uint32_t w; uint32_t h; float evMin; float evMax; };
    const HistoPC histoPC { w, h, m_evMin, m_evMax };

    struct AdaptPC { float dt; float adaptSpeed; float evMin; float evMax;
                     float lowPct; float highPct; };
    const AdaptPC adaptPC { dt, m_adaptSpeed, m_evMin, m_evMax, m_lowPct, m_highPct };

    ctx.rg->AddPass("AE_Histogram",
        [hHisto, handles](RGPassBuilder& b) {
            b.Read(handles.hdr);
            b.WriteBuffer(hHisto);
        },
        [histoPipeline, histoSet, histoPC, w, h]
        (RHI::IRHICommandList& cmd, const RGResources& /*res*/) {
            cmd.SetComputePipeline(histoPipeline);
            cmd.SetDescriptorSet(1, histoSet);
            cmd.SetPushConstants(&histoPC, sizeof(histoPC), RHI::RHIShaderStage::Compute);
            cmd.Dispatch((w + 15u) / 16u, (h + 15u) / 16u, 1u);
        });

    ctx.rg->AddPass("AE_Adapt",
        [hHisto, hExposure](RGPassBuilder& b) {
            b.ReadBuffer(hHisto);
            b.WriteBuffer(hExposure);
        },
        [adaptPipeline, adaptSet, adaptPC, hExposure, expStaging]
        (RHI::IRHICommandList& cmd, const RGResources& res) {
            cmd.SetComputePipeline(adaptPipeline);
            cmd.SetDescriptorSet(1, adaptSet);
            cmd.SetPushConstants(&adaptPC, sizeof(adaptPC), RHI::RHIShaderStage::Compute);
            cmd.Dispatch(1u, 1u, 1u);
            // Copy updated exposure to staging buffer for CPU readback next frame.
            // Manual barriers: StorageWrite→CopySrc for the copy, then CopySrc→StorageWrite
            // to leave the buffer in the state declared by next frame's ImportBuffer.
            const RHI::RHIBufferHandle expBuf = res.GetBuffer(hExposure);
            cmd.BufferBarrier(expBuf,
                RHI::RHIBufferState::StorageWrite, RHI::RHIBufferState::CopySrc);
            cmd.CopyBuffer(expBuf, expStaging, 0, 0, sizeof(float));
            cmd.BufferBarrier(expBuf,
                RHI::RHIBufferState::CopySrc, RHI::RHIBufferState::StorageWrite);
        });
}

void SceneRenderer::AutoExposureFeature::ReadbackExposure(RHI::IRHIDevice* device)
{
    if (!m_exposureStaging.IsValid()) return;
    float value = 1.0f;
    device->ReadBufferData(m_exposureStaging, &value, sizeof(float));
    if (std::isfinite(value) && value > 0.f)
        m_currentExposure = value;
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

    // 32³ RGBA16F 3D LUT for parametric color grading
    RHI::RHITextureDesc lutDesc{};
    lutDesc.width     = 32;
    lutDesc.height    = 32;
    lutDesc.depth     = 32;
    lutDesc.format    = RHI::RHIFormat::RGBA16F;
    lutDesc.usage     = RHI::RHITextureUsage::UnorderedAccess
                      | RHI::RHITextureUsage::Sampled;
    lutDesc.debugName = "CG_LUT";
    m_cgLutTex = ctx.device->CreateTexture(lutDesc);

    const std::string spvPath  = ctx.shaderDir + "/postfx_cg_bake.comp.spv";
    const std::string reflPath = ctx.shaderDir + "/postfx_cg_bake.comp.refl";
    const auto spv = LoadComputeSpv(spvPath);
    if (spv.empty()) {
        SA_LOG_WARN("TonemapFeature: postfx_cg_bake.comp.spv not found — color grading disabled");
        return;
    }
    if (!m_cgBakeProg.Load(ctx.device, {spv, LoadComputeRefl(reflPath)})) {
        SA_LOG_WARN("TonemapFeature: ComputeProgram load failed — color grading disabled");
        return;
    }

    m_cgBakeDs = ctx.device->AllocateDescriptorSet(m_cgBakeProg.GetLayout(0));
    ctx.device->WriteDescriptorStorageImage(m_cgBakeDs, 0, m_cgLutTex);
}

void SceneRenderer::TonemapFeature::OnShutdown(RHI::IRHIDevice* device)
{
    m_cgBakeProg.Unload(device);
    if (m_cgLutTex.IsValid()) device->DestroyTexture(m_cgLutTex);
    m_cgLutTex    = {};
    m_cgBakeDs    = {};
    m_cgLutReady  = false;
    m_hdrDescSet = {};
    m_type       = nullptr;
}

void SceneRenderer::TonemapFeature::BakeColorGrading(RHI::IRHIDevice& device)
{
    if (!m_cgLutTex.IsValid() || !m_cgBakeProg.IsLoaded() || !m_cgBakeDs.IsValid()) return;

    const auto pipeline = m_cgBakeProg.GetPipeline(&device);
    if (!pipeline.IsValid()) return;

    struct BakePC {
        glm::vec3 lift;    float _p0;
        glm::vec3 midtone; float _p1;
        glm::vec3 gain;    float _p2;
        float saturation;
        float contrast;
        float _p3; float _p4;
    };
    const BakePC pc{
        m_cgSettings.lift,    0.f,
        m_cgSettings.midtone, 0.f,
        m_cgSettings.gain,    0.f,
        m_cgSettings.saturation,
        m_cgSettings.contrast,
        0.f, 0.f
    };

    using RS = RHI::RHIResourceState;
    const RS srcState = m_cgLutReady ? RS::ShaderRead : RS::Undefined;
    device.ImmediateCompute([&](RHI::IRHICommandList* cmd) {
        cmd->TransitionTexture(m_cgLutTex, srcState, RS::UnorderedAccess);
        cmd->SetComputePipeline(pipeline);
        cmd->SetDescriptorSet(0, m_cgBakeDs);
        cmd->SetPushConstants(&pc, sizeof(pc), RHI::RHIShaderStage::Compute);
        cmd->Dispatch(4, 4, 8);  // 32/8, 32/8, 32/4
        cmd->TransitionTexture(m_cgLutTex, RS::UnorderedAccess, RS::ShaderRead);
    });
    m_cgLutReady = true;

    if (m_hdrDescSet.IsValid())
        device.WriteDescriptorTexture(m_hdrDescSet, 1, m_cgLutTex);
}

void SceneRenderer::TonemapFeature::SetColorGrading(const ColorGradingSettings& s)
{
    if (s.enabled    != m_cgSettings.enabled    ||
        s.lift       != m_cgSettings.lift        ||
        s.midtone    != m_cgSettings.midtone     ||
        s.gain       != m_cgSettings.gain        ||
        s.saturation != m_cgSettings.saturation  ||
        s.contrast   != m_cgSettings.contrast)
    {
        m_cgDirty = true;
    }
    m_cgSettings = s;
}

void SceneRenderer::TonemapFeature::AddPasses(SceneRenderer& /*renderer*/,
                                               const FrameContext& ctx,
                                               const RendererHandles& handles,
                                               const entt::registry& /*reg*/,
                                               uint32_t w, uint32_t h)
{
    if (!m_type || !m_hdrDescSet.IsValid()) return;

    if (m_cgDirty) {
        BakeColorGrading(*ctx.device);
        m_cgDirty = false;
    }

    ctx.BindTexture(m_hdrDescSet, 0, handles.hdr);

    AttachmentKey swapKey{};
    swapKey.colorCount      = 1;
    swapKey.colorFormats[0] = ctx.device->GetSwapchainFormat();
    swapKey.depthFormat     = RHI::RHIFormat::Undefined;

    const RHI::RHIPipelineHandle pipeline = m_type->GetOrCreatePipeline(ctx.device, swapKey);

    const RHI::RHIDescSetHandle frameSet   = ctx.frameSet;
    const RHI::RHIDescSetHandle hdrDescSet = m_hdrDescSet;
    const RGTextureHandle rgHdr       = handles.hdr;
    const RGTextureHandle rgSwapchain = handles.swapchain;

    struct TonemapPC { float exposure; float cgEnabled; float _pad0; float _pad1; };
    const float cgOn = (m_cgSettings.enabled && m_cgLutTex.IsValid() && m_cgBakeProg.IsLoaded()) ? 1.f : 0.f;
    const TonemapPC pc{m_exposure, cgOn, 0.f, 0.f};

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
            cmd.SetDescriptorSet(1, frameSet);
            cmd.SetDescriptorSet(2, hdrDescSet);
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

    ctx.BindTexture(m_hdrLutDescSet, 0, handles.hdr);

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
            cmd.SetDescriptorSet(1, frameSet);
            cmd.SetDescriptorSet(2, hdrLutDescSet);
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

void SceneRenderer::BloomFeature::RebuildDescSets(int newMipCount, RHI::IRHIDevice* device)
{
    device->FreeDescriptorSet(m_thresholdDescSet);
    for (int i = 0; i < m_mipCount - 1; ++i) {
        device->FreeDescriptorSet(m_downsampleDescSet[i]);
        device->FreeDescriptorSet(m_upsampleDescSet[i]);
    }
    device->FreeDescriptorSet(m_compositeDescSet);

    m_mipCount = newMipCount;
    const auto layout = m_thresholdType->shader.GetMaterialLayout();
    m_thresholdDescSet = device->AllocateDescriptorSet(layout);
    for (int i = 0; i < m_mipCount - 1; ++i) {
        m_downsampleDescSet[i] = device->AllocateDescriptorSet(layout);
        m_upsampleDescSet[i]   = device->AllocateDescriptorSet(layout);
    }
    m_compositeDescSet = device->AllocateDescriptorSet(layout);
}

void SceneRenderer::BloomFeature::AddPasses(SceneRenderer& renderer,
                                             const FrameContext& ctx,
                                             const RendererHandles& handles,
                                             const entt::registry& /*reg*/,
                                             uint32_t w, uint32_t h)
{
    if (!m_enabled) return;
    if (!m_thresholdType || !m_downsampleType || !m_upsampleType || !m_compositeType) return;
    if (!m_thresholdDescSet.IsValid()) return;

    // Threshold reads taaResolved: the anti-aliased pre-bloom frame (= hdr when TAA disabled).
    ctx.BindTexture(m_thresholdDescSet, 0, handles.taaResolved);
    for (int i = 0; i < m_mipCount - 1; ++i)
        ctx.BindTexture(m_downsampleDescSet[i], 0, handles.bloomMip[i]);
    for (int i = 0; i < m_mipCount - 1; ++i)
        ctx.BindTexture(m_upsampleDescSet[i], 0, handles.bloomMip[m_mipCount - 1 - i]);
    ctx.BindTexture(m_compositeDescSet, 0, handles.bloomMip[0]);

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
    const RGTextureHandle rgHdr      = handles.hdr;          // composite writes here
    const RGTextureHandle rgTaaInput = handles.taaResolved;  // threshold reads here (= rgHdr when TAA off)

    // ── Threshold: taaResolved → mip[0] ──────────────────────────────────────
    struct ThresholdPC { float threshold; float knee; float p0; float p1; };
    const ThresholdPC threshPC{m_threshold, m_threshold * 0.1f, 0.f, 0.f};
    {
        const RGTextureHandle dst = rgMip[0];
        const uint32_t tw = mipW[0], th = mipH[0];
        ctx.rg->AddPass("BloomThreshold",
            [rgTaaInput, dst](RGPassBuilder& b) { b.Read(rgTaaInput); b.Write(dst); },
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
                cmd.SetDescriptorSet(1, frameSet);
                cmd.SetDescriptorSet(2, threshDescSet);
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
                cmd.SetDescriptorSet(1, frameSet);
                cmd.SetDescriptorSet(2, ds);
                cmd.Draw(3, 1, 0, 0);
                cmd.EndRenderPass();
            });
    }

    // ── Upsample: mip[i+1] → mip[i], additive accumulation ──────────────────
    // Iterates i = m_mipCount-2 downto 0 (fine → coarse).
    // usSet[passIdx] was bound to mip[m_mipCount-1-passIdx], matching src each time.
    // Per-layer radius decays geometrically from m_radius: each level scales by 0.85.
    struct UpsamplePC { float radius; float p0, p1, p2; };
    float upsampleRadii[kMaxBloomMips - 1];
    upsampleRadii[0] = m_radius;
    for (int k = 1; k < m_mipCount - 1; ++k)
        upsampleRadii[k] = upsampleRadii[k - 1] * 0.85f;
    for (int i = m_mipCount - 2; i >= 0; --i) {
        const int passIdx           = (m_mipCount - 2) - i;
        const RGTextureHandle src   = rgMip[i + 1];
        const RGTextureHandle dst   = rgMip[i];
        const uint32_t dw = mipW[i], dh = mipH[i];
        const RHI::RHIDescSetHandle us = usSet[passIdx];
        const UpsamplePC upPC{upsampleRadii[passIdx], 0.f, 0.f, 0.f};
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
                cmd.SetDescriptorSet(1, frameSet);
                cmd.SetDescriptorSet(2, us);
                cmd.SetPushConstants(&upPC, sizeof(upPC), RHI::RHIShaderStage::Fragment);
                cmd.Draw(3, 1, 0, 0);
                cmd.EndRenderPass();
            });
    }

    // ── Composite: mip[0] → HDR (additive, preserves lighting) ──────────────
    struct CompositePC { float strength; float p0; float p1; float p2; };
    const CompositePC compPC{m_strength, 0.f, 0.f, 0.f};
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
                cmd.SetDescriptorSet(1, frameSet);
                cmd.SetDescriptorSet(2, compositeDescSet);
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
                    // Read+Write on rgSwap is load-bearing: RenderGraph builds
                    // dependency edges only on Read-after-Write, so without the
                    // Read this pass has no edge to Tonemap (which writes rgSwap)
                    // and the topo sort schedules it before Tonemap — Tonemap
                    // then overwrites the debug lines.
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
                    cmd.SetDescriptorSet(1, frameSet);
                    cmd.SetDescriptorSet(2, descSet);
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
                    // Read+Write on rgSwap — needed for the topo sort to place
                    // this pass after Tonemap (see DebugOverlay above).
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
                    cmd.SetDescriptorSet(1, frameSet);
                    cmd.SetDescriptorSet(2, xrayDescSet);
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

    if (!ctx.matMgr->LoadShaderProgram(m_skinnedProgram,
                                        "selection_mask_skinned", "selection_mask", ctx))
        SA_LOG_WARN("SelectionMaskFeature: skinned shader not found — skinned outlines disabled");
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
        RHI::RHIBufferHandle  vb, ib;
        uint32_t              firstIndex, indexCount;
        int32_t               vertexOffset;
        glm::mat4             model;
        bool                  isSkinned = false;
        RHI::RHIDescSetHandle skinDescSet;
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
                       wt->matrix * di.subLocalTransform,
                       di.isSkinned, di.skinDescSet});
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

    RHI::RHIPipelineHandle skinnedPipeline{};
    if (m_skinnedProgram.IsLoaded())
        skinnedPipeline = m_skinnedProgram.GetOrCreatePipeline(ctx.device, maskKey);

    const RHI::RHIDescSetHandle frameSet = ctx.frameSet;
    const RGTextureHandle       rgMask   = handles.selectionMask;

    ctx.rg->AddPass("SelectionMask",
        [rgMask](RGPassBuilder& b) {
            b.Write(rgMask);
        },
        [pipeline, skinnedPipeline, frameSet, dcs = std::move(dcs), rgMask, w, h]
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

            RHI::RHIPipelineHandle currentPipeline{};
            bool frameSetBound = false;
            for (const auto& dc : dcs) {
                const RHI::RHIPipelineHandle effectivePipeline =
                    (dc.isSkinned && skinnedPipeline.IsValid()) ? skinnedPipeline : pipeline;
                if (effectivePipeline.index != currentPipeline.index) {
                    cmd.SetPipeline(effectivePipeline);
                    currentPipeline = effectivePipeline;
                    if (!frameSetBound) {
                        cmd.SetDescriptorSet(1, frameSet);
                        frameSetBound = true;
                    }
                }
                if (dc.isSkinned && dc.skinDescSet.IsValid())
                    cmd.SetDescriptorSet(3, dc.skinDescSet);  // Step 6.5: skin at set=3
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
                cmd.SetDescriptorSet(1, frameSet);
                cmd.SetDescriptorSet(2, dilateHDesc);
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
                // Read+Write on rgSwap so the topo sort orders this after
                // Tonemap (which writes rgSwap). Without the Read, the RG
                // has no Write→Write edge and may run this before Tonemap.
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
                cmd.SetDescriptorSet(1, frameSet);
                cmd.SetDescriptorSet(2, outlineDesc);
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
            // Read+Write on rgSwap so the topo sort orders this after Tonemap
            // (the RG only builds Read-after-Write edges; pure Write→Write does
            // not constrain order, so without the Read this pass may run
            // before Tonemap and get overwritten).
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
            cmd.SetDescriptorSet(1, frameSet);
            cmd.Draw(3, 1, 0, 0);
            cmd.EndRenderPass();
        });
}

} // namespace StellarAlia
