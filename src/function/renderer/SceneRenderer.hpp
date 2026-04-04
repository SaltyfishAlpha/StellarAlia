#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "function/FrameUniforms.hpp"
#include "function/renderer/CameraData.hpp"
#include "function/FrameUniformsBuffer.hpp"
#include "function/ibl/GpuIblBake.hpp"
#include "function/ibl/GpuLtcBake.hpp"
#include "function/material/MaterialInstance.hpp"
#include "function/material/MaterialManager.hpp"
#include "function/material/ShaderProgram.hpp"
#include "function/render_graph/RenderGraph.hpp"
#include "function/renderer/RenderFeature.hpp"
#include "function/scene/Scene.hpp"
#include "platform/rhi/IRHIDevice.hpp"
#include "platform/rhi/RHITypes.hpp"
#include "resource/ResourceManager.hpp"

namespace StellarAlia {

// ─────────────────────────────────────────────────────────────────────────────
// RendererConfig — toggles and parameters for the built-in render pipeline.
//
// Pass via SceneRenderer::Desc::config before calling Init().
// All fields have sensible defaults; only override what you need.
// ─────────────────────────────────────────────────────────────────────────────
struct RendererConfig {
    // ── Shadow pass ───────────────────────────────────────────────────────────
    bool     shadowEnabled  = true;   // directional shadow map pass
    uint32_t shadowMapSize  = 2048;   // texel resolution (width = height)

    // ── Bloom pass ────────────────────────────────────────────────────────────
    bool bloomEnabled       = true;   // multi-scale pyramid bloom
    int  bloomMipCount      = 3;      // pyramid depth [2, kMaxBloomMips]; more = wider glow

    // ── Tonemap pass ─────────────────────────────────────────────────────────
    // true  → built-in ACES tonemap runs automatically after bloom.
    // false → no tonemap is registered; the caller is expected to add their own
    //         RenderFeature that reads m_rgHdr and writes to the swapchain.
    bool builtinTonemap     = true;
};

// ─────────────────────────────────────────────────────────────────────────────
// SceneRenderer
//
// Owns the RenderGraph, FrameUniformsBuffer, depth texture, and all
// RenderFeatures — including the built-in skybox and geometry passes, which
// are themselves implemented as (private) RenderFeature subclasses and
// pre-registered at the front of the feature list during Init().
//
// Typical usage:
//
//   // Setup (once):
//   renderer.AddFeature(std::make_unique<MyFeature>());  // before Init
//   renderer.Init(desc);
//   renderer.SetIBL(scene.GetWorldSettings());
//   renderer.BuildDrawList(scene);
//
//   // Per-frame — single entry point:
//   scene.UpdateTransforms();
//   renderer.RenderFrame(scene, w, h);
// ─────────────────────────────────────────────────────────────────────────────
class SceneRenderer {
public:
    struct Desc {
        RHI::IRHIDevice*           device      = nullptr;
        MaterialManager*           matMgr      = nullptr;
        Resource::ResourceManager* resMgr      = nullptr;
        std::string                shaderDir;      // path to compiled .spv/.refl files
        std::string                cookCacheDir;   // path to cook cache
        RendererConfig             config;         // pipeline feature toggles + parameters
    };

    // Initialise FrameUniformsBuffer, shaders, PBR MaterialType, depth placeholder,
    // and pre-register built-in Skybox + Geometry features at the front of the list.
    // Returns false on shader load failure.
    bool Init(const Desc& desc);

    // Release all owned GPU resources and call OnShutdown on all features.
    void Shutdown();

    // ── Per-scene setup ───────────────────────────────────────────────────────

    // Offline-first IBL: load cooked assets if present, GPU-bake + cache otherwise.
    // SH9 coefficients are stored internally for use in RenderFrame.
    // Returns false only when the scene has no HDR source at all.
    bool SetIBL(const WorldSettings& ws);

    // Build (or rebuild) the per-entity draw list. Loads GPU meshes, resolves
    // materials via 3-tier fallback, pre-bakes pipelines.
    void BuildDrawList(Scene& scene);

    // ── Feature registration ──────────────────────────────────────────────────

    // Register a user RenderFeature. If Init() has already been called,
    // OnInit is invoked immediately; otherwise it is deferred to Init().
    // User features always execute after the built-in skybox and geometry passes.
    void AddFeature(std::unique_ptr<RenderFeature> feature);

    // ── Render tick ───────────────────────────────────────────────────────────
    //
    // Overload 1 — explicit camera (editor camera, cinematic camera, etc.)
    //   The renderer uses the supplied CameraData directly; the Scene is only
    //   queried for lights and draw items.
    //
    // Overload 2 — camera from scene (game runtime path)
    //   Extracts the first entity tagged ActiveCameraTag from the Scene and
    //   derives CameraData from its CameraComponent + WorldTransformComponent.
    //   Equivalent to calling Overload 1 with ExtractCamera(scene, w, h).
    //
    // Both overloads execute one complete frame:
    //   Phase 1 — collect frame data (camera + lights).
    //   Phase 2 — BeginFrame, upload uniforms, reset + build RenderGraph,
    //             Compile + Execute, [uiPass if provided], EndFrame, Present.
    //
    // Returns immediately (no-op) when device->BeginFrame returns null.
    //
    // uiPass — optional callback invoked after 3D passes complete but before
    //   EndFrame. The active IRHICommandList is passed in. Use this to record
    //   ImGui draw calls (cast to VulkanCommandList to get VkCommandBuffer).
    using UIPassFn = std::function<void(RHI::IRHICommandList*)>;
    void RenderFrame(Scene& scene, const CameraData& camera, uint32_t w, uint32_t h,
                     UIPassFn uiPass = {});
    void RenderFrame(Scene& scene, uint32_t w, uint32_t h);

    // Extract CameraData from the Scene's active camera entity.
    // Returns identity matrices when no ActiveCameraTag entity is found.
    [[nodiscard]] static CameraData ExtractCamera(const Scene& scene,
                                                   uint32_t w, uint32_t h);

    [[nodiscard]] bool IsReady() const { return m_ready; }

private:
    struct DrawItem {
        entt::entity                      entity;
        glm::mat4                         subLocalTransform;
        RHI::RHIBufferHandle              vertexBuffer;
        RHI::RHIBufferHandle              indexBuffer;
        uint32_t                          firstIndex;
        uint32_t                          indexCount;
        int32_t                           vertexOffset;
        MaterialInstance*                 material;
        std::unique_ptr<MaterialInstance> ownedMaterial;
        RHI::RHIPipelineHandle            pipeline;
        uint32_t                          pushConstantSize;
    };

    // ── Built-in features — private inner classes ─────────────────────────────
    //
    // Nested classes have access to SceneRenderer's private members (C++11 §11.7).
    // Both are pre-registered at the front of m_features during Init().

    // Renders the scene depth-only from the directional light's perspective,
    // producing a 2048×2048 shadow map sampled by DeferredLightingFeature.
    // Light-space matrix comes from FrameUniforms (set by RenderFrame each tick).
    class ShadowFeature final : public RenderFeature {
    public:
        void OnInit(const FeatureInitContext& ctx) override;
        void AddPasses(SceneRenderer& renderer, const FrameContext& ctx,
                       const RendererHandles& handles, const entt::registry& reg,
                       uint32_t w, uint32_t h) override;
    private:
        MaterialType* m_type = nullptr;
    };

    // Renders a fullscreen skybox into the HDR buffer (raw HDR, no tonemap).
    class SkyboxFeature final : public RenderFeature {
    public:
        void OnInit(const FeatureInitContext& ctx) override;
        void AddPasses(SceneRenderer& renderer, const FrameContext& ctx,
                       const RendererHandles& handles, const entt::registry& reg,
                       uint32_t w, uint32_t h) override;
    private:
        MaterialType* m_type = nullptr;
    };

    // Registers the "PBR" material type (deferred_geometry shaders) and default
    // material instance in OnInit, then renders all draw items into the G-Buffer
    // (3 MRT + depth, with depth clear) in AddPasses.
    class GBufferFeature final : public RenderFeature {
    public:
        explicit GBufferFeature(SceneRenderer* owner) : m_owner(owner) {}
        void OnInit(const FeatureInitContext& ctx) override;
        void AddPasses(SceneRenderer& renderer, const FrameContext& ctx,
                       const RendererHandles& handles, const entt::registry& reg,
                       uint32_t w, uint32_t h) override;
    private:
        SceneRenderer* m_owner = nullptr;
    };

    // Fullscreen deferred lighting: reads G-Buffer, writes HDR with AlphaBlend
    // (alpha=0 for background pixels preserves the skybox written earlier).
    class DeferredLightingFeature final : public RenderFeature {
    public:
        void OnInit(const FeatureInitContext& ctx) override;
        void AddPasses(SceneRenderer& renderer, const FrameContext& ctx,
                       const RendererHandles& handles, const entt::registry& reg,
                       uint32_t w, uint32_t h) override;
    private:
        MaterialType*         m_type = nullptr;
        RHI::RHIDescSetHandle m_gbDescSet;
        uint32_t              m_trackedW = 0;
        uint32_t              m_trackedH = 0;
    };

    // Fullscreen ACES tonemap: reads HDR buffer, writes swapchain.
    class TonemapFeature final : public RenderFeature {
    public:
        void OnInit(const FeatureInitContext& ctx) override;
        void AddPasses(SceneRenderer& renderer, const FrameContext& ctx,
                       const RendererHandles& handles, const entt::registry& reg,
                       uint32_t w, uint32_t h) override;
    private:
        MaterialType*         m_type = nullptr;
        RHI::RHIDescSetHandle m_hdrDescSet;
        uint32_t              m_trackedW = 0;
        uint32_t              m_trackedH = 0;
    };

    // Hard upper bound for bloom pyramid array allocation.
    // Actual depth at runtime is RendererConfig::bloomMipCount (clamped to this).
    static constexpr int kMaxBloomMips = 8;

    // Multi-scale pyramid bloom: threshold → N× downsample → N× upsample → composite.
    // Runs after DeferredLighting, before Tonemap.
    class BloomFeature final : public RenderFeature {
    public:
        explicit BloomFeature(int mipCount) : m_mipCount(mipCount) {}
        void OnInit(const FeatureInitContext& ctx) override;
        void AddPasses(SceneRenderer& renderer, const FrameContext& ctx,
                       const RendererHandles& handles, const entt::registry& reg,
                       uint32_t w, uint32_t h) override;
    private:
        int                   m_mipCount       = 6;
        MaterialType*         m_thresholdType  = nullptr;
        MaterialType*         m_downsampleType = nullptr;
        MaterialType*         m_upsampleType   = nullptr;
        MaterialType*         m_compositeType  = nullptr;
        RHI::RHIDescSetHandle m_thresholdDescSet;
        RHI::RHIDescSetHandle m_downsampleDescSet[kMaxBloomMips - 1];
        RHI::RHIDescSetHandle m_upsampleDescSet[kMaxBloomMips - 1];
        RHI::RHIDescSetHandle m_compositeDescSet;
        uint32_t              m_trackedW = 0;
        uint32_t              m_trackedH = 0;
    };

    // ── Persistent state ──────────────────────────────────────────────────────
    bool m_ready = false;

    RHI::IRHIDevice*           m_device      = nullptr;
    MaterialManager*           m_matMgr      = nullptr;
    Resource::ResourceManager* m_resMgr      = nullptr;
    std::string                m_shaderDir;
    std::string                m_cookCacheDir;

    RendererConfig             m_config;            // stored from Desc at Init time
    int                        m_bloomMipCount = 0; // resolved from m_config at Init

    FrameUniformsBuffer        m_frameUniforms;   // owned — created in Init
    glm::vec4                  m_shCoeffs[9] = {};  // stored by SetIBL
    uint32_t                   m_frameCount  = 0;   // incremented per RenderFrame

    GpuIblBake                 m_iblBake;
    GpuLtcBake                 m_ltcBake;

    std::unique_ptr<MaterialInstance> m_defaultMaterial;
    std::vector<DrawItem>             m_drawItems;

    std::vector<std::unique_ptr<RenderFeature>> m_features;

    // ── Shadow map (fixed 2048×2048, never resized) ───────────────────────────
    RHI::RHITextureHandle m_shadowMap;
    RGTextureHandle       m_rgShadowMap;

    // ── G-Buffer render targets + HDR composite buffer ─────────────────────
    // All resized together with the viewport in RenderFrame.
    RHI::RHITextureHandle m_gbRT0;    // RGBA8_UNORM  albedo.rgb + occlusion.a
    RHI::RHITextureHandle m_gbRT1;    // RGBA16F      oct-normal(RG) + roughness(B) + metallic(A)
    RHI::RHITextureHandle m_gbRT2;    // RGBA16F      emissive.rgb
    RHI::RHITextureHandle m_hdrTex;   // RGBA16F      skybox + deferred-lit composite
    uint32_t              m_gbWidth   = 0;
    uint32_t              m_gbHeight  = 0;

    // ── Bloom pyramid (m_bloomMipCount active levels, arrays sized to kMaxBloomMips) ──
    RHI::RHITextureHandle m_bloomMip[kMaxBloomMips];
    uint32_t              m_bloomMipW[kMaxBloomMips] = {};
    uint32_t              m_bloomMipH[kMaxBloomMips] = {};
    RGTextureHandle       m_rgBloomMip[kMaxBloomMips];

    // ── Owned depth texture (resized in RenderFrame) ──────────────────────────
    RHI::RHITextureHandle m_depthTex;
    uint32_t              m_depthWidth  = 0;
    uint32_t              m_depthHeight = 0;

    // ── Per-frame state (valid during RenderFrame execution) ──────────────────
    RHI::IRHICommandList* m_cmd          = nullptr;
    RHI::RHIDescSetHandle m_frameDescSet;
    uint32_t              m_frameWidth   = 0;
    uint32_t              m_frameHeight  = 0;

    RenderGraph     m_rg;
    RGTextureHandle m_rgSwapchain;
    RGTextureHandle m_rgDepth;
    RGTextureHandle m_rgGbRT0;
    RGTextureHandle m_rgGbRT1;
    RGTextureHandle m_rgGbRT2;
    RGTextureHandle m_rgHdr;

    // ── Frame data helpers (private, called from RenderFrame) ─────────────────
    [[nodiscard]] LightUniforms GatherLights(const Scene& scene) const;
    static void ApplyCameraToUniforms(const CameraData& cam, FrameUniforms& fu);
};

} // namespace StellarAlia
