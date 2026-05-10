#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "core/spatial/BVHTree.hpp"
#include "function/debug/DebugDraw.hpp"
#include "function/FrameUniforms.hpp"
#include "function/renderer/CameraData.hpp"
#include "function/FrameUniformsBuffer.hpp"
#include "function/ibl/GpuIblBake.hpp"
#include "function/ibl/GpuLtcBake.hpp"
#include "function/material/MaterialInstance.hpp"
#include "function/material/ComputeProgram.hpp"
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
    // bloomEnabled / bloom params are in WorldSettings::pp (runtime hot-swap).
    int  bloomMipCount      = 3;      // pyramid depth [2, kMaxBloomMips]; requires rebuild to change
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
    // On a fresh GPU bake, assigns new AssetIDs to ws.brdfLut / prefilteredEnv /
    // skyboxCubemap / sh9 and caches them to cookCacheDir so the next call can
    // skip the bake.  Returns false only when the scene has no HDR source at all.
    bool SetIBL(WorldSettings& ws);

    // Apply WorldSettings to the live renderer: updates background color/mode,
    // calls SetIBL for IBL assets, and replaces the tonemap feature if the mode
    // changes (Builtin ↔ LUT).  Calls WaitIdle before replacing GPU resources.
    // Pass updateIBL=false to skip the IBL load/bake step (e.g. for live param
    // updates where IBL assets have not changed).
    void ApplyWorldSettings(WorldSettings& ws, bool updateIBL = true);

    // Delete cached IBL files (.satex / .sash9) for the current baked products,
    // clear their AssetIDs from ws, then immediately re-bake from ws.skyboxHdr.
    void RebakeIBL(WorldSettings& ws);

    // Must be called after ClearProjectAssets() and before the next
    // ApplyWorldSettings() on a project switch.  Restores m_cachedBrdfLut to the
    // Init-baked LUT so ApplyWorldSettings(SolidColor) does not write a dangling
    // VkImageView (from a prior Skybox scene) into the frame-uniform descriptor.
    void ResetProjectIBL();

    // Build (or rebuild) the per-entity draw list. Loads GPU meshes, resolves
    // materials via 3-tier fallback, pre-bakes pipelines.
    void BuildDrawList(Scene& scene);

    // ── Feature registration ──────────────────────────────────────────────────

    // Register a user RenderFeature. If Init() has already been called,
    // OnInit is invoked immediately; otherwise it is deferred to Init().
    // User features always execute after the built-in skybox and geometry passes.
    void AddFeature(std::unique_ptr<RenderFeature> feature);

    // Access the MaterialManager used by this renderer (e.g. for editor drawers).
    [[nodiscard]] MaterialManager* GetMaterialManager() const { return m_matMgr; }

    // Bind a DebugDraw source for the per-frame line overlay pass.
    // Call after Init(). The overlay is a no-op when dd is null or has no lines.
    void SetDebugDraw(DebugDraw* dd) { m_debugDraw = dd; }

    // Set the entity whose silhouette should be outlined this frame.
    // Pass entt::null to disable the outline.
    void SetSelectedEntity(entt::entity e) { m_selectionEntity = e; }

    // Set the screen-space dilation radius for the selection outline (pixels).
    void SetOutlineWidth(float px) { m_selectionOutlineWidth = px; }

    // Enable or disable the infinite XZ grid overlay.
    void SetInfiniteGrid(bool enabled) { m_infiniteGrid = enabled; }

    // Update the cook cache path used for IBL bake output.
    // Call this when the active project changes.
    void SetCookCacheDir(const std::string& path) { m_cookCacheDir = path; }

    // Apply project-specific shader types after a project switch.
    // Clears old project MaterialTypes, hot-swaps deferred_lighting.frag when
    // custom shading models were cooked, and registers new project types.
    // cookedShaderDir — directory containing the project's cooked .spv / .refl files,
    //                   or empty string when the project has no .saglsl files.
    // Device must be idle (call after ClearProjectInstances()).
    void ApplyProjectShaderTypes(const std::string& cookedShaderDir);

    // ── Render tick ───────────────────────────────────────────────────────────
    //
    // Overload 1 — explicit camera (editor camera, cinematic camera, etc.)
    //   The renderer uses the supplied CameraData directly; the Scene is only
    //   queried for lights and draw items.
    //
    // Overload 2 — camera from scene (game runtime path)
    //   Extracts the highest-priority CameraComponent entity from the Scene
    //   and derives CameraData from its WorldTransformComponent.
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
    // Returns identity matrices when no CameraComponent entity is found.
    [[nodiscard]] static CameraData ExtractCamera(const Scene& scene,
                                                   uint32_t w, uint32_t h);

    [[nodiscard]] bool IsReady() const { return m_ready; }
    [[nodiscard]] const RenderGraph& GetRenderGraph() const { return m_rg; }

    // Returns the set=2 descriptor layout for GPU skinning (bone matrices + skin data SSBOs).
    // Valid after Init(); used by AnimationSystem::Init to allocate per-entity skinDescSets.
    [[nodiscard]] RHI::RHIDescLayoutHandle GetSkinDescLayout() const;

    // Ray-cast against the scene BVH (render mesh AABBs).
    // Returns the nearest hit entity, or entt::null when nothing was hit.
    // Intended for editor mouse-picking (Issue #30).
    [[nodiscard]] entt::entity RaycastScene(const Core::Ray& ray,
                                            float maxDist = 1e30f) const;

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
        // Entity-local AABB (subLocalTransform applied). Used by BVH culling.
        // skipCull = true for skinned meshes (animated AABB not computed).
        glm::vec3                         localAABBMin { 1e30f};
        glm::vec3                         localAABBMax {-1e30f};
        // World-space AABB after model transform. Used by RaycastScene Phase B.
        glm::vec3                         worldAABBMin { 1e30f};
        glm::vec3                         worldAABBMax {-1e30f};
        bool                              skipCull  = false;
        bool                              isSkinned = false;
        RHI::RHIDescSetHandle             skinDescSet;   // set=2; valid when isSkinned=true
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
        ShaderProgram m_skinnedShadowProgram;  // shadow_skinned.vert + shadow.frag
    };

    // Renders a fullscreen skybox into the HDR buffer (raw HDR, no tonemap).
    // In SolidColor mode, just clears the HDR buffer to m_backgroundColor.
    class SkyboxFeature final : public RenderFeature {
    public:
        void OnInit(const FeatureInitContext& ctx) override;
        void AddPasses(SceneRenderer& renderer, const FrameContext& ctx,
                       const RendererHandles& handles, const entt::registry& reg,
                       uint32_t w, uint32_t h) override;

        WorldSettings::BackgroundMode m_backgroundMode  = WorldSettings::BackgroundMode::SolidColor;
        glm::vec3                     m_backgroundColor = { 0.08f, 0.08f, 0.08f };
    private:
        MaterialType* m_type = nullptr;
    };

    // Registers the "PBR" material type (deferred_geometry shaders) and default
    // material instance in OnInit, then renders all draw items into the G-Buffer
    // (3 MRT + depth, with depth clear) in AddPasses.
    class GBufferFeature final : public RenderFeature {
        friend class SceneRenderer;
    public:
        explicit GBufferFeature(SceneRenderer* owner) : m_owner(owner) {}
        void OnInit(const FeatureInitContext& ctx) override;
        void AddPasses(SceneRenderer& renderer, const FrameContext& ctx,
                       const RendererHandles& handles, const entt::registry& reg,
                       uint32_t w, uint32_t h) override;
    private:
        SceneRenderer*           m_owner          = nullptr;
        ShaderProgram            m_skinnedProgram;   // deferred_geometry_skinned.vert + _geometry.frag
        RHI::RHIDescLayoutHandle m_skinDescLayout;   // set=2 layout from m_skinnedProgram
    };

    // Fullscreen deferred lighting: reads G-Buffer, writes HDR with AlphaBlend
    // (alpha=0 for background pixels preserves the skybox written earlier).
    class DeferredLightingFeature final : public RenderFeature {
    public:
        void OnInit(const FeatureInitContext& ctx) override;
        void AddPasses(SceneRenderer& renderer, const FrameContext& ctx,
                       const RendererHandles& handles, const entt::registry& reg,
                       uint32_t w, uint32_t h) override;

        // Hot-reload the frag shader from recompiled bytes.
        // Device must be idle before calling.
        void ReloadShaders(RHI::IRHIDevice*              device,
                           std::span<const uint8_t>      fragSpv,
                           const RHI::ShaderReflection&  fragRefl);
    private:
        MaterialType*         m_type = nullptr;
        RHI::RHIDescSetHandle m_gbDescSet;
    };

    // GTAO ambient occlusion: 3-pass (main + H blur + V blur).
    // Always runs: when disabled writes 1.0 via a single fast pass.
    class SSAOFeature final : public RenderFeature {
    public:
        void OnInit    (const FeatureInitContext& ctx) override;
        void OnShutdown(RHI::IRHIDevice* device)       override;
        void AddPasses (SceneRenderer& renderer, const FrameContext& ctx,
                        const RendererHandles& handles, const entt::registry& reg,
                        uint32_t w, uint32_t h) override;

        bool  m_enabled       = false;
        float m_radius        = 32.f;
        float m_strength      = 1.0f;
        float m_bias          = 0.025f;
        int   m_directions    = 8;
        int   m_steps         = 4;
        float m_blurSharpness = 10.f;
    private:
        MaterialType*         m_gtaoType = nullptr;
        MaterialType*         m_blurType = nullptr;
        RHI::RHIDescSetHandle m_gtaoDescSet;
        RHI::RHIDescSetHandle m_blurHDescSet;
        RHI::RHIDescSetHandle m_blurVDescSet;
    };

    // Temporal Anti-Aliasing: depth-based reprojection + history blend + neighborhood clamp.
    // When disabled, transparently passes handles.hdr through (m_outputHandle = handles.hdr).
    class TAAFeature final : public RenderFeature {
        friend class SceneRenderer;
    public:
        void OnInit    (const FeatureInitContext& ctx) override;
        void OnShutdown(RHI::IRHIDevice* device)       override;
        void AddPasses (SceneRenderer& renderer, const FrameContext& ctx,
                        const RendererHandles& handles, const entt::registry& reg,
                        uint32_t w, uint32_t h) override;

        // Set by AddPasses each frame; RenderFrame redirects handles.taaResolved to this.
        RGTextureHandle           m_outputHandle;

        bool  m_enabled       = false;
        float m_blendStatic   = 0.1f;
        float m_blendMotion   = 0.5f;
        bool  m_antiGhosting  = true;
    private:
        MaterialType*         m_taaType = nullptr;
        RHI::RHIDescSetHandle m_resolveSet;  // set=1: binding0=current, binding1=history, binding2=depth
        // Ping-pong history textures: prevIndex is read, currIndex is written each frame.
        // The resolve pass writes directly to history[currIndex]; m_outputHandle = rgHistoryWrite.
        RHI::RHITextureHandle m_historyTex[2];
        int                   m_historyIndex = 0;  // index of the texture last written (= next read)
        uint32_t              m_trackedW = 0, m_trackedH = 0;
        bool                  m_historyValid = false;
    };

    // Fullscreen ACES tonemap: reads HDR buffer, writes swapchain.
    // Optionally applies parametric color grading via a baked 32³ RGBA16F 3D LUT.
    class TonemapFeature final : public RenderFeature {
    public:
        void OnInit    (const FeatureInitContext& ctx) override;
        void OnShutdown(RHI::IRHIDevice* device)       override;
        void AddPasses (SceneRenderer& renderer, const FrameContext& ctx,
                        const RendererHandles& handles, const entt::registry& reg,
                        uint32_t w, uint32_t h) override;

        // Called by ApplyWorldSettings whenever pp.colorGrading changes.
        void SetColorGrading(const ColorGradingSettings& s);

        float              m_exposure  = 1.f;
    private:
        void BakeColorGrading(RHI::IRHIDevice& device);

        MaterialType*          m_type      = nullptr;
        RHI::RHIDescSetHandle  m_hdrDescSet;

        RHI::RHITextureHandle  m_cgLutTex;
        ComputeProgram         m_cgBakeProg;
        RHI::RHIDescSetHandle  m_cgBakeDs;
        ColorGradingSettings   m_cgSettings;
        bool                   m_cgDirty   = true;
        bool                   m_cgLutReady = false;
    };

    // LUT-based tonemap: ACES + color grading from a 2D strip LUT.
    class LutTonemapFeature final : public RenderFeature {
    public:
        void OnInit    (const FeatureInitContext& ctx) override;
        void OnShutdown(RHI::IRHIDevice* device)       override;
        void AddPasses (SceneRenderer& renderer, const FrameContext& ctx,
                        const RendererHandles& handles, const entt::registry& reg,
                        uint32_t w, uint32_t h) override;

        void SetLutTexture(RHI::IRHIDevice* device, RHI::RHITextureHandle tex);

        float m_exposure    = 1.f;
        float m_lutStrength = 1.f;
    private:
        MaterialType*         m_type = nullptr;
        RHI::RHIDescSetHandle m_hdrLutDescSet;
    };

    // Editor line overlay: reads DebugDraw vertex data each frame, renders after Tonemap.
    // noVertexInput pipeline; vertex positions sourced from a CPU-visible SSBO.
    class DebugOverlayFeature final : public RenderFeature {
    public:
        void OnInit    (const FeatureInitContext& ctx)          override;
        void OnShutdown(RHI::IRHIDevice* device)               override;
        void AddPasses (SceneRenderer& renderer, const FrameContext& ctx,
                        const RendererHandles& handles, const entt::registry& reg,
                        uint32_t w, uint32_t h)                override;
    private:
        MaterialType*         m_type        = nullptr;
        MaterialType*         m_xrayType    = nullptr;   // depth-test-disabled variant
        RHI::RHIBufferHandle  m_ssbo;                    // depth-tested lines SSBO
        RHI::RHIBufferHandle  m_xraySsbo;                // always-on-top lines SSBO
        RHI::RHIDescSetHandle m_descSet;
        RHI::RHIDescSetHandle m_xrayDescSet;
    };

    // Renders the selected entity's geometry into a 1-channel R8 mask texture for
    // depth-correct outline extraction. Runs after DeferredLighting so the existing
    // scene depth can be used to occlude hidden parts of the selection.
    class SelectionMaskFeature final : public RenderFeature {
    public:
        explicit SelectionMaskFeature(SceneRenderer* owner) : m_owner(owner) {}
        void OnInit   (const FeatureInitContext& ctx) override;
        void AddPasses(SceneRenderer& renderer, const FrameContext& ctx,
                       const RendererHandles& handles, const entt::registry& reg,
                       uint32_t w, uint32_t h) override;
    private:
        SceneRenderer* m_owner          = nullptr;
        MaterialType*  m_type           = nullptr;
        ShaderProgram  m_skinnedProgram;  // selection_mask_skinned.vert + selection_mask.frag
    };

    // Fullscreen infinite XZ grid rendered at the Y=0 plane.
    // Ray-plane intersection in the fragment shader; fwidth anti-aliasing;
    // gl_FragDepth for correct geometry occlusion; distance fade-out.
    class InfiniteGridFeature final : public RenderFeature {
    public:
        void OnInit   (const FeatureInitContext& ctx) override;
        void AddPasses(SceneRenderer& renderer, const FrameContext& ctx,
                       const RendererHandles& handles, const entt::registry& reg,
                       uint32_t w, uint32_t h) override;
    private:
        MaterialType* m_type = nullptr;
    };

    // Two-pass separable dilation outline:
    //   Pass 1 (DilateH): reads selectionMask, writes horizontally-dilated R8 intermediate.
    //   Pass 2 (Composite): reads dilateH + selectionMask, writes outline to swapchain.
    class SelectionOutlineFeature final : public RenderFeature {
    public:
        void OnInit    (const FeatureInitContext& ctx) override;
        void OnShutdown(RHI::IRHIDevice* device)       override;
        void AddPasses (SceneRenderer& renderer, const FrameContext& ctx,
                        const RendererHandles& handles, const entt::registry& reg,
                        uint32_t w, uint32_t h) override;
    private:
        MaterialType*         m_dilateHType   = nullptr;  // horizontal pass
        MaterialType*         m_type          = nullptr;  // composite pass
        RHI::RHIDescSetHandle m_dilateHDescSet;
        RHI::RHIDescSetHandle m_descSet;
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

        // Runtime parameters — updated by SceneRenderer::ApplyWorldSettings each call.
        bool  m_enabled   = true;
        float m_threshold = 1.0f;
        float m_strength  = 0.4f;
        float m_radius    = 1.0f;  // widest upsample level; each level scales by ×0.85
        // Must be called after WaitIdle; frees old desc sets and reallocates for newMipCount.
        void RebuildDescSets(int newMipCount, RHI::IRHIDevice* device);
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
    };

    // GPU histogram → exponential-smoothing exposure adaptation.
    // Runs after DeferredLighting on the raw HDR buffer; skipped when disabled.
    class AutoExposureFeature final : public RenderFeature {
        friend class SceneRenderer;
    public:
        void OnInit    (const FeatureInitContext& ctx) override;
        void OnShutdown(RHI::IRHIDevice* device)       override;
        void AddPasses (SceneRenderer& renderer, const FrameContext& ctx,
                        const RendererHandles& handles, const entt::registry& reg,
                        uint32_t w, uint32_t h) override;

        // Called once per frame after BeginFrame to bring the previous frame's
        // GPU-computed exposure value back to CPU for the tonemap push constant.
        void ReadbackExposure(RHI::IRHIDevice* device);

        bool  m_enabled     = false;
        float m_evMin       = -4.0f;
        float m_evMax       =  4.0f;
        float m_adaptSpeed  =  2.0f;
        float m_lowPct      =  0.45f;
        float m_highPct     =  0.95f;
        float m_currentExposure = 1.0f;
    private:
        ComputeProgram        m_histoProg;
        ComputeProgram        m_adaptProg;
        RHI::RHIBufferHandle  m_exposureSsbo;    // device-local, Storage|CopySrc
        RHI::RHIBufferHandle  m_exposureStaging; // CPU-visible, CopyDst
        RHI::RHIDescSetHandle m_histoSet;        // set=1: binding0=HDR tex, binding1=histo SSBO
        RHI::RHIDescSetHandle m_adaptSet;        // set=1: binding0=histo SSBO, binding1=exposure SSBO
        bool                  m_timerInit = false;
        std::chrono::steady_clock::time_point m_lastTime;
    };

    // ── Persistent state ──────────────────────────────────────────────────────
    bool m_ready = false;

    RHI::IRHIDevice*           m_device      = nullptr;
    MaterialManager*           m_matMgr      = nullptr;
    Resource::ResourceManager* m_resMgr      = nullptr;
    std::string                m_shaderDir;
    std::string                m_cookCacheDir;

    RendererConfig             m_config;              // stored from Desc at Init time
    int                        m_bloomMipCount        = 0;  // resolved from m_config at Init
    int                        m_pendingBloomMipCount = -1; // deferred mip count change

    FrameUniformsBuffer        m_frameUniforms;   // owned — created in Init
    glm::vec4                  m_shCoeffs[9] = {};  // stored by SetIBL
    uint32_t                   m_frameCount  = 0;   // incremented per RenderFrame

    GpuIblBake                 m_iblBake;
    GpuLtcBake                 m_ltcBake;

    std::unique_ptr<MaterialInstance>    m_defaultMaterial;
    std::vector<DrawItem>                m_drawItems;

    // Spatial acceleration — built once per BuildDrawList, queried per frame.
    Core::BVHTree<entt::entity>          m_bvh;
    std::vector<entt::entity>            m_visibleEntities;   // BVH Query output
    std::vector<const DrawItem*>         m_visibleDrawItems;  // filtered per RenderFrame

    std::vector<std::unique_ptr<RenderFeature>> m_features;

    // Raw pointers into m_features — stable as long as the vector doesn't reallocate.
    // Set during Init, updated by ApplyWorldSettings on tonemap replacement.
    GBufferFeature*          m_gbufferFeature          = nullptr;
    DeferredLightingFeature* m_deferredLightingFeature = nullptr;
    SkyboxFeature*           m_skyboxFeature           = nullptr;
    BloomFeature*         m_bloomFeature   = nullptr;
    SSAOFeature*          m_ssaoFeature    = nullptr;
    TAAFeature*           m_taaFeature     = nullptr;
    AutoExposureFeature*  m_aeFeature      = nullptr;
    RenderFeature*        m_tonemapFeature = nullptr;  // either TonemapFeature or LutTonemapFeature

    // ── Solid-color ambient environment ──────────────────────────────────────
    // A 1×1 RGBA32F cubemap filled with backgroundColor.  Written to
    // t_PrefilteredEnv in SolidColor mode so metallic surfaces reflect the
    // background instead of sampling the black placeholder.
    RHI::RHITextureHandle m_solidAmbientCube;
    glm::vec3             m_solidAmbientColor = { -1.f, -1.f, -1.f };  // sentinel

    // BRDF LUT baked at Init time — lives outside ResourceManager, never destroyed
    // on project switch.  Restored into m_cachedBrdfLut by ResetProjectIBL().
    RHI::RHITextureHandle m_bakeBrdfLut;

    // BRDF LUT from the most recent successful IBL load/bake.
    // Reused when switching to SolidColor so specular split-sum stays correct.
    // MUST be reset to m_bakeBrdfLut after ClearProjectAssets() — see ResetProjectIBL().
    RHI::RHITextureHandle m_cachedBrdfLut;

    // ── TAA temporal state ────────────────────────────────────────────────────
    glm::mat4  m_prevUnjitteredViewProj = glm::mat4(1.f);  // last frame's unjittered VP
    uint32_t   m_haltonIndex            = 0;               // Halton sequence position
    uint32_t   m_frameIndex             = 0;               // frame counter mod 256

    // ── Debug overlay ─────────────────────────────────────────────────────────
    DebugDraw*  m_debugDraw      = nullptr;  // set by SetDebugDraw; not owned
    glm::mat4   m_currentViewProj = glm::mat4(1.f);  // updated each RenderFrame

    // ── Infinite grid ─────────────────────────────────────────────────────────
    bool m_infiniteGrid = false;

    // ── Selection outline ─────────────────────────────────────────────────────
    entt::entity          m_selectionEntity      = entt::null;
    float                 m_selectionOutlineWidth = 2.f;
    RHI::RHITextureHandle m_selectionMask;
    RGTextureHandle       m_rgSelectionMask;
    RHI::RHITextureHandle m_dilateH;          // R8 horizontal-dilation intermediate
    RGTextureHandle       m_rgDilateH;
    uint32_t              m_selectionMaskW  = 0;
    uint32_t              m_selectionMaskH  = 0;

    // ── Shadow map (fixed 2048×2048, never resized) ───────────────────────────
    RHI::RHITextureHandle m_shadowMap;
    RGTextureHandle       m_rgShadowMap;

    // ── G-Buffer render targets + HDR composite buffer ─────────────────────
    // All resized together with the viewport in RenderFrame.
    RHI::RHITextureHandle m_gbRT0;    // RGBA8_UNORM  albedo.rgb + occlusion.a
    RHI::RHITextureHandle m_gbRT1;    // RGBA16F      oct-normal(RG) + roughness(B) + metallic(A)
    RHI::RHITextureHandle m_gbRT2;    // RGBA16F      emissive.rgb
    // m_hdrTex removed — HDR_Color is transient; managed by the RG slot pool.
    uint32_t              m_gbWidth   = 0;
    uint32_t              m_gbHeight  = 0;

    // ── SSAO result texture (R8_UNORM, full-res, resized in RenderFrame) ────────
    RHI::RHITextureHandle m_ssaoTex;
    RGTextureHandle       m_rgSsaoTex;

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
    void ApplyCameraToUniforms(const CameraData& cam, FrameUniforms& fu, uint32_t w, uint32_t h);

    // Shuts down the current tonemap feature, initialises the replacement, and
    // updates m_tonemapFeature.  Caller must call WaitIdle before invoking this.
    void ReplaceTonemapFeature(std::unique_ptr<RenderFeature> newFeature,
                               const FeatureInitContext& ctx);

    // Creates (or recreates) m_solidAmbientCube with the given color.
    // No-op when the color has not changed since the last call.
    void UpdateSolidAmbientCube(glm::vec3 color);
};

} // namespace StellarAlia
