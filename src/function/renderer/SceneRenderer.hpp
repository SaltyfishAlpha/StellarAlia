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
#include "function/material/ProgramCache.hpp"
#include "function/material/MaterialParamRing.hpp"
#include "function/material/ScreenEffectRegistry.hpp"
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
// Issue #56 — which geometry runs through the depth prepass.
enum class DepthPrepassMode : uint8_t {
    MaskedOnly,  // default: only MASK (alpha-test) geometry; opaque goes straight to GBuffer
    Full,        // opaque+mask prewritten, GBuffer all-EQUAL (zero overdraw) — NOT YET IMPLEMENTED
};

struct RendererConfig {
    // ── Shadow pass ───────────────────────────────────────────────────────────
    bool     shadowEnabled  = true;   // directional shadow map pass
    uint32_t shadowMapSize  = 2048;   // texel resolution (width = height)

    // ── Bloom pass ────────────────────────────────────────────────────────────
    // bloomEnabled / bloom params are in WorldSettings::pp (runtime hot-swap).
    int  bloomMipCount      = 3;      // pyramid depth [2, kMaxBloomMips]; requires rebuild to change

    // ── Depth prepass (Issue #56) ─────────────────────────────────────────────
    // Full falls back to MaskedOnly with a warning until CF1/CF2 land.
    DepthPrepassMode depthPrepassMode = DepthPrepassMode::MaskedOnly;
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

    // Catalog of cooked ScreenEffect types (Issue #88). Editor reads this to
    // populate the "Add Effect" menu and draw per-effect @Param widgets.
    [[nodiscard]] ScreenEffectRegistry& GetScreenEffectRegistry() { return m_screenEffectRegistry; }

    // Bind a DebugDraw source for the per-frame line overlay pass.
    // Call after Init(). The overlay is a no-op when dd is null or has no lines.
    void SetDebugDraw(DebugDraw* dd) { m_debugDraw = dd; }

    // Set the entity whose silhouette should be outlined this frame.
    // Pass entt::null to disable the outline.
    void SetSelectedEntity(entt::entity e) { m_selectionEntity = e; }

    // Set the screen-space dilation radius for the selection outline (pixels).
    void SetOutlineWidth(float px) { m_selectionOutlineWidth = px; }

    // Issue #102: highlight a single material slot (submesh) instead of the
    // whole selection subtree. slot < 0 or entt::null disables; takes priority
    // over SetSelectedEntity in the selection mask when active.
    void SetHighlightSlot(entt::entity e, int32_t slot) {
        m_highlightEntity = e;
        m_highlightSlot   = slot;
    }

    // ── Issue #102: GPU ID picking ────────────────────────────────────────────
    // Request an ID pick at a swapchain pixel: the next RenderFrame records a
    // one-shot pass writing each DrawItem's 1-based index to an R32_UINT buffer
    // (own depth → nearest surface wins), read back after the frame completes.
    void RequestIdPick(uint32_t px, uint32_t py);

    struct PickResult {
        entt::entity entity       = entt::null;
        uint32_t     submeshIndex = 0;
        bool         hit          = false;   // false = clicked background
    };
    // Returns true exactly once per completed pick and fills `out`.
    bool TryConsumePickResult(PickResult& out);

    // X-8 debug view: render the ID pass every frame and composite it as
    // per-submesh hash colors over the swapchain. Zero cost when off.
    void SetDebugIdView(bool on) { m_debugIdView = on; }

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
    // Drains the GPU itself, so it is safe between frames or at RenderFrame top.
    void ApplyProjectShaderTypes(const std::string& cookedShaderDir);

    // Request a project shader/effect re-register (Issue #88) from a mid-frame
    // context (e.g. AssetsPanel reimport/create during UI draw). Destroying GPU
    // programs/pipelines mid-command-buffer is unsafe, so this only records the
    // request; RenderFrame applies it at the next frame's safe point.
    void RequestProjectShaderReload(const std::string& cookedShaderDir) {
        m_pendingShaderReloadDir = cookedShaderDir;
        m_hasPendingShaderReload = true;
    }

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

    // Issue #84: layout for VelocityPrepass skinned vert (set=3 bindings 0/1/2).
    // Invalid if velocity_prepass_skinned shader unavailable — AnimationSystem
    // then skips velocityDescSet allocation per entity.
    [[nodiscard]] RHI::RHIDescLayoutHandle GetVelocityDescLayout() const;

private:
    // Issue #56: scene depth carries a stencil plane (deferred-lighting stencil
    // masking). Shadow map stays D32F. Swap to D32F_S8 here if D24 precision
    // ever becomes a problem.
    static constexpr RHI::RHIFormat kSceneDepthFormat = RHI::RHIFormat::D24_S8;

    struct DrawItem {
        entt::entity                      entity;
        // Issue #102: index into GPUMesh::subMeshes (== MeshRenderer slot index).
        uint32_t                          submeshIndex = 0;
        glm::mat4                         subLocalTransform;
        RHI::RHIBufferHandle              vertexBuffer;
        RHI::RHIBufferHandle              indexBuffer;
        uint32_t                          firstIndex;
        uint32_t                          indexCount;
        int32_t                           vertexOffset;
        MaterialInstance*                 material;
        std::unique_ptr<MaterialInstance> ownedMaterial;        // unused once SSBO ring path fully in (kept for legacy fallback)
        RHI::RHIPipelineHandle            pipeline;
        uint32_t                          pushConstantSize;
        // Issue #72: dynamic offset into MaterialParamRing for the per-draw blob.
        // Only meaningful when item.material->GetType()->usesMaterialParamsSSBO.
        uint32_t                          materialUboOffset = 0;
        // Issue #56: transparency classification from the material asset.
        // Static per material — safe to bake here (draw list is rebuilt on
        // material dirty). Back-to-front distance is per-frame and therefore
        // computed at collection time in ForwardTransparentFeature, not here.
        AlphaMode                         alphaMode   = AlphaMode::Opaque;
        bool                              doubleSided = false;
        // Entity-local AABB (subLocalTransform applied). Used by BVH culling.
        // skipCull = true for skinned meshes (animated AABB not computed).
        glm::vec3                         localAABBMin { 1e30f};
        glm::vec3                         localAABBMax {-1e30f};
        // World-space AABB after model transform. Used by the transparent
        // pass's back-to-front centroid sort (Issue #56).
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
        ShaderProgram* m_skinnedShadowProgram = nullptr;  // shadow_skinned.vert + shadow.frag (ProgramCache-owned)
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

    // Issue #56 — depth prepass. Always emits one depth-only pass that clears
    // depth+stencil (GBuffer therefore always Loads), then draws MASK geometry
    // with the alpha-test discard frag, writing depth + stencil=1. Vertex
    // shaders are the deferred_geometry pair — bit-exact depth vs GBuffer.
    class DepthPrepassFeature final : public RenderFeature {
    public:
        void OnInit(const FeatureInitContext& ctx) override;
        void AddPasses(SceneRenderer& renderer, const FrameContext& ctx,
                       const RendererHandles& handles, const entt::registry& reg,
                       uint32_t w, uint32_t h) override;
    private:
        ShaderProgram* m_maskProgram        = nullptr;  // deferred_geometry.vert + depth_prepass_mask.frag
        ShaderProgram* m_maskSkinnedProgram = nullptr;  // deferred_geometry_skinned.vert + same frag
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
        ShaderProgram*           m_skinnedProgram = nullptr;  // deferred_geometry_skinned.vert (ProgramCache-owned)
        RHI::RHIDescLayoutHandle m_skinDescLayout;   // set=2 layout from m_skinnedProgram
    };

    // Issue #84 — Per-object velocity prepass.
    // Runs after GBuffer (depth populated). For each visible DrawItem, samples
    // currModel + prevModel push constants (and curr/prev bone matrices for
    // skinned meshes) to write per-pixel (currUV − prevUV) into handles.velocity.
    // Skipped entirely when MotionBlurFeature is disabled.
    class VelocityPrepassFeature final : public RenderFeature {
        friend class SceneRenderer;
    public:
        void OnInit    (const FeatureInitContext& ctx) override;
        void OnShutdown(RHI::IRHIDevice* device)       override;
        void AddPasses (SceneRenderer& renderer, const FrameContext& ctx,
                        const RendererHandles& handles, const entt::registry& reg,
                        uint32_t w, uint32_t h) override;

        // Layout exposed for AnimationSystem to allocate per-entity velocityDescSet
        // (set=3 with bindings 0=curr, 1=skinData, 2=prev).
        RHI::RHIDescLayoutHandle GetSkinnedLayout() const { return m_skinnedLayout; }

    private:
        ShaderProgram*           m_staticProgram = nullptr;   // velocity_prepass.{vert,frag} (ProgramCache-owned)
        ShaderProgram*           m_skinnedProgram = nullptr;  // velocity_prepass_skinned.vert (ProgramCache-owned)
        RHI::RHIDescLayoutHandle m_skinnedLayout;    // reflected set=3 with bindings 0/1/2
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

    // Issue #56 — forward translucency (BLEND materials). Issue #105 moved it
    // BEFORE TAA (after SSR): blends in place into handles.hdr (a plain
    // transient at that point, safe to RMW) so transparents get anti-aliased
    // with the rest of the frame, and renders an R8 coverage mask that TAA uses
    // to cut history weight on transparent pixels (no velocity → would ghost).
    class ForwardTransparentFeature final : public RenderFeature {
        friend class SceneRenderer;
    public:
        void OnInit(const FeatureInitContext& ctx) override;
        void AddPasses(SceneRenderer& renderer, const FrameContext& ctx,
                       const RendererHandles& handles, const entt::registry& reg,
                       uint32_t w, uint32_t h) override;

        // Set when BLEND items were drawn this frame AND TAA is enabled;
        // TAAFeature reads it as the reactive mask (invalid → no reactivity).
        RGTextureHandle m_reactiveMask;

    private:
        static void DrawItems(RHI::IRHICommandList& cmd,
                              const std::vector<std::pair<float, const DrawItem*>>& items,
                              const RHI::RHIPipelineHandle* pipes,
                              const entt::registry& reg,
                              RHI::RHIDescSetHandle bindlessSet,
                              RHI::RHIDescSetHandle frameSet);

        ShaderProgram*        m_program                = nullptr;  // deferred_geometry.vert + forward_transparent.frag
        ShaderProgram*        m_skinnedProgram         = nullptr;  // deferred_geometry_skinned.vert + same frag
        ShaderProgram*        m_reactiveProgram        = nullptr;  // deferred_geometry.vert + transparent_reactive.frag
        ShaderProgram*        m_reactiveSkinnedProgram = nullptr;  // skinned variant
    };

    // Screen Space Reflections (Issue #48): single compute pass after
    // DeferredLighting, before TAA. Replaces the IBL env-probe specular with
    // screen-traced colour where confident; relies on TAA to denoise the
    // ray-march jitter. When disabled, AddPasses leaves m_outputHandle invalid
    // so the feature loop keeps handles.hdr unchanged.
    class SSRFeature final : public RenderFeature {
        friend class SceneRenderer;
    public:
        void OnInit    (const FeatureInitContext& ctx) override;
        void OnShutdown(RHI::IRHIDevice* device)       override;
        void AddPasses (SceneRenderer& renderer, const FrameContext& ctx,
                        const RendererHandles& handles, const entt::registry& reg,
                        uint32_t w, uint32_t h) override;

        // Set by AddPasses when enabled; RenderFrame redirects handles.hdr to this.
        RGTextureHandle  m_outputHandle;

        bool  m_enabled      = false;
        float m_maxRoughness = 0.4f;
        int   m_maxSteps     = 64;
        float m_thickness    = 0.1f;
        float m_strength     = 1.0f;
    private:
        ComputeProgram*       m_prog = nullptr;  // owned by ProgramCache (Issue #86) — trace
        ComputeProgram*       m_resolveProg  = nullptr;  // Phase C spatial resolve
        ComputeProgram*       m_temporalProg = nullptr;  // Phase C temporal accumulation
        RHI::RHIDescSetHandle m_ssrSet;         // trace set=2
        RHI::RHIDescSetHandle m_resolveSet;     // resolve set=2
        RHI::RHIDescSetHandle m_temporalSet;    // temporal set=2

        // Ping-pong reflection history for temporal accumulation (Phase C). Persistent,
        // rebuilt on resize; read reprojected from [1-idx], written to [idx] each frame.
        RHI::RHITextureHandle m_ssrHistory[2];
        uint32_t              m_histW = 0, m_histH = 0;
        int                   m_histIdx = 0;
        bool                  m_histValid = false;

        // Hi-Z min depth pyramid (Issue #89): built each frame via #94 SPD, sampled by
        // the ray march. R32F mip-chain, rebuilt on resize.
        ComputeProgram*       m_hizCopyProg = nullptr;  // depth → Hi-Z mip0
        ComputeProgram*       m_hizSpdProg  = nullptr;  // min-reduce mip1..N (SPD)
        RHI::RHIDescSetHandle m_hizCopySet;
        RHI::RHIDescSetHandle m_hizSpdSet;
        RHI::RHITextureHandle m_hizTex;
        uint32_t              m_hizW = 0, m_hizH = 0, m_hizMips = 0;
    };

    // Volumetric fog (Issue #49): froxel single scattering. Inject evaluates
    // media + local light per froxel, Scatter integrates transmittance front-to-
    // back, Apply composites hdr·T + inscatter (after SSR, pre-TAA).
    class VolumetricFogFeature final : public RenderFeature {
        friend class SceneRenderer;
    public:
        void OnInit    (const FeatureInitContext& ctx) override;
        void OnShutdown(RHI::IRHIDevice* device)       override;
        void AddPasses (SceneRenderer& renderer, const FrameContext& ctx,
                        const RendererHandles& handles, const entt::registry& reg,
                        uint32_t w, uint32_t h) override;

        // Set by AddPasses when enabled; RenderFrame redirects handles.hdr to this.
        RGTextureHandle m_outputHandle;
        // Integrated froxel volume of the current frame (invalid when disabled).
        // ForwardTransparentFeature declares an RG Read on it (Step 9).
        RGTextureHandle m_integratedHandle;

        bool      m_enabled       = false;
        float     m_density       = 0.02f;
        glm::vec3 m_albedo        = {0.9f, 0.9f, 0.9f};
        float     m_anisotropy    = 0.6f;
        float     m_distance      = 64.f;
        float     m_heightBase    = 0.f;
        float     m_heightFalloff = 0.f;
        float     m_ambient       = 0.2f;
    private:
        ComputeProgram*       m_injectProg  = nullptr;  // owned by ProgramCache
        ComputeProgram*       m_scatterProg = nullptr;
        ShaderProgram*        m_applyProg   = nullptr;
        RHI::RHIDescSetHandle m_injectSet;   // set=2 (shadow map + media UAV)
        RHI::RHIDescSetHandle m_scatterSet;  // set=0 (media + integrated UAV)
        RHI::RHIDescSetHandle m_applySet;    // set=2 (hdr + depth + volume)
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
        // Issue #105: transient copy of the resolve (PostTAA_HDR); RenderFrame
        // redirects handles.hdr here. The copy exists because downstream RMW
        // writers (BloomComposite UAV add) must never touch the ping-pong
        // history in place — next frame reads it back (progressive-brightness).
        RGTextureHandle           m_postTaaHandle;

        bool  m_enabled       = false;
        float m_blendStatic   = 0.1f;
        float m_blendMotion   = 0.5f;
        bool  m_antiGhosting  = true;
    private:
        MaterialType*         m_taaType = nullptr;
        RHI::RHIDescSetHandle m_resolveSet;  // set=1: binding0=current, binding1=history, binding2=depth
        ShaderProgram*        m_copyProgram = nullptr;  // fullscreen_tri + forward_copy.frag (Issue #105)
        RHI::RHIDescSetHandle m_copyDescSet;            // copy set=2 (t_Source = historyWrite)
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
        ComputeProgram*        m_cgBakeProg = nullptr;  // owned by ProgramCache (Issue #86)
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
        ShaderProgram* m_skinnedProgram = nullptr;  // selection_mask_skinned.vert (ProgramCache-owned)
    };

    // Issue #102: editor mouse picking. When a pick is pending, renders every
    // DrawItem's 1-based index into the R32_UINT pick buffer with its own
    // transient depth; SceneRenderer resolves the readback after the frame.
    class IdPickFeature final : public RenderFeature {
    public:
        explicit IdPickFeature(SceneRenderer* owner) : m_owner(owner) {}
        void OnInit   (const FeatureInitContext& ctx) override;
        void AddPasses(SceneRenderer& renderer, const FrameContext& ctx,
                       const RendererHandles& handles, const entt::registry& reg,
                       uint32_t w, uint32_t h) override;
    private:
        SceneRenderer*        m_owner          = nullptr;
        MaterialType*         m_type           = nullptr;
        ShaderProgram*        m_skinnedProgram = nullptr;  // id_pass_skinned.vert
        // X-8 debug view: fullscreen palette composite of the ID buffer.
        MaterialType*         m_viewType       = nullptr;  // id_debug_view.frag
        RHI::RHIDescSetHandle m_viewDescSet;
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
        // Issue #92: all bloom passes on compute (threshold/downsample: pure write;
        // upsample/composite: read-modify-write UAV, replacing hardware additive blend).
        ComputeProgram*       m_thresholdProg  = nullptr;
        ComputeProgram*       m_downsampleProg = nullptr;
        ComputeProgram*       m_upsampleProg   = nullptr;
        ComputeProgram*       m_compositeProg  = nullptr;
        RHI::RHIDescSetHandle m_thresholdDescSet;
        RHI::RHIDescSetHandle m_downsampleDescSet[kMaxBloomMips - 1];
        RHI::RHIDescSetHandle m_upsampleDescSet[kMaxBloomMips - 1];
        RHI::RHIDescSetHandle m_compositeDescSet;
    };

    // Depth of Field: CoC from depth → separable near/far blur → composite.
    // Runs after Bloom and before Tonemap; when disabled is a zero-cost no-op.
    class DoFFeature final : public RenderFeature {
    public:
        void OnInit    (const FeatureInitContext& ctx) override;
        void OnShutdown(RHI::IRHIDevice* device)       override;
        void AddPasses (SceneRenderer& renderer, const FrameContext& ctx,
                        const RendererHandles& handles, const entt::registry& reg,
                        uint32_t w, uint32_t h) override;

        // Set by AddPasses; RenderFrame redirects handles.hdr to this when valid.
        RGTextureHandle m_outputHandle;

        bool  m_enabled     = false;
        float m_focusDist   = 5.0f;
        float m_aperture    = 1.4f;
        float m_focalLength = 50.0f;
        int   m_samples     = 16;
        float m_maxCocPx    = 20.0f;
    private:
        MaterialType*         m_cocMat       = nullptr;
        MaterialType*         m_blurMat      = nullptr;
        MaterialType*         m_compositeMat = nullptr;
        RHI::RHIDescSetHandle m_cocDescSet;
        RHI::RHIDescSetHandle m_blurDescSets[4]; // near-H, near-V, far-H, far-V
        RHI::RHIDescSetHandle m_compositeDescSet;
    };

    // Issue #46 Phase 1 — Camera Motion Blur (4 fullscreen passes:
    // VelocityFill / TileMax / NeighborMax / Reconstruct).  Reads handles.depth,
    // writes handles.velocity (public RG resource), produces in-place hdr blur
    // via m_outputHandle redirect.  Phase 2 will replace VelocityFill with
    // per-object writes from GBuffer; the rest stays unchanged.
    class MotionBlurFeature final : public RenderFeature {
    public:
        void OnInit    (const FeatureInitContext& ctx) override;
        void OnShutdown(RHI::IRHIDevice* device)       override;
        void AddPasses (SceneRenderer& renderer, const FrameContext& ctx,
                        const RendererHandles& handles, const entt::registry& reg,
                        uint32_t w, uint32_t h) override;

        // Set by AddPasses on a successful run; RenderFrame redirects handles.hdr.
        RGTextureHandle m_outputHandle;

        bool  m_enabled  = false;
        float m_strength = 0.5f;
        int   m_samples  = 8;
        float m_maxSpeed = 0.1f;

        static constexpr int kTileSize = 16;
    private:
        // Issue #84: handles.velocity is now filled by VelocityPrepassFeature;
        // MB_Velocity pass + m_velocityMat / m_velocityDescSet removed.
        MaterialType*         m_tileMaxMat     = nullptr;
        MaterialType*         m_neighborMaxMat = nullptr;
        MaterialType*         m_reconstructMat = nullptr;
        RHI::RHIDescSetHandle m_tileMaxDescSet;
        RHI::RHIDescSetHandle m_neighborMaxDescSet;
        RHI::RHIDescSetHandle m_reconstructDescSet;
    };

    // Issue #47 — Screen modifications.  Runs after Tonemap on the LDR buffer
    // (`handles.ldr` → `handles.swapchain`).  Single fullscreen pass that
    // optionally applies vignette / chromatic aberration / film grain.  When
    // all three effects are disabled the shader degrades to a single texture
    // copy; the pass is never skipped so swapchain always gets written.
    class PostFXFeature final : public RenderFeature {
    public:
        void OnInit    (const FeatureInitContext& ctx) override;
        void OnShutdown(RHI::IRHIDevice* device)       override;
        void AddPasses (SceneRenderer& renderer, const FrameContext& ctx,
                        const RendererHandles& handles, const entt::registry& reg,
                        uint32_t w, uint32_t h) override;

        bool  m_vignetteEnabled    = false;
        float m_vignetteIntensity  = 0.4f;
        float m_vignetteSmoothness = 0.6f;
        bool  m_caEnabled          = false;
        float m_caStrength         = 0.5f;
        bool  m_filmGrainEnabled   = false;
        float m_filmGrainIntensity = 0.1f;
        float m_filmGrainSize      = 1.6f;
    private:
        MaterialType*         m_type = nullptr;
        RHI::RHIDescSetHandle m_descSet;
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
        ComputeProgram*       m_histoProg = nullptr;  // owned by ProgramCache (Issue #86)
        ComputeProgram*       m_adaptProg = nullptr;  // owned by ProgramCache (Issue #86)
        RHI::RHIBufferHandle  m_exposureSsbo;    // device-local, Storage|CopySrc
        RHI::RHIBufferHandle  m_exposureStaging; // CPU-visible, CopyDst
        RHI::RHIDescSetHandle m_histoSet;        // set=1: binding0=HDR tex, binding1=histo SSBO
        RHI::RHIDescSetHandle m_adaptSet;        // set=1: binding0=histo SSBO, binding1=exposure SSBO
        bool                  m_timerInit = false;
        std::chrono::steady_clock::time_point m_lastTime;
    };

    // Generic executor for declarative ScreenEffects (Issue #88). One anchor
    // instance per injection point; runs each registered effect at its point:
    // binds named engine resources + params, draws, and chains handles.hdr.
    // Users author .saeffect — this engine-owned feature is the only executor.
    class ScreenEffectFeature final : public RenderFeature {
        friend class SceneRenderer;
    public:
        explicit ScreenEffectFeature(EffectInject inject) : m_inject(inject) {}

        // Reads the owning SceneRenderer's registry + active per-scene stack
        // (m_screenEffectRegistry / m_activeScreenEffects) — hence nested.
        void AddPasses(SceneRenderer& renderer, const FrameContext& ctx,
                       const RendererHandles& handles, const entt::registry& reg,
                       uint32_t w, uint32_t h) override;

        // Set by AddPasses; SceneRenderer redirects handles.hdr to this when valid.
        RGTextureHandle      m_outputHandle;
        [[nodiscard]] EffectInject Inject() const { return m_inject; }

    private:
        EffectInject m_inject;
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
    ProgramCache               m_programCache;    // owned — GPU program owner (Issue #86)
    ScreenEffectRegistry       m_screenEffectRegistry;  // owned — cooked .saeffect catalog (Issue #88)

    // Active per-scene ScreenEffect stack, resolved from PostProcessSettings in
    // ApplyWorldSettings: name + enable + a param blob built from the type's
    // defaults overlaid with the instance's named overrides. Anchors read this.
    struct ActiveScreenEffect { std::string name; bool enabled = true; std::vector<uint8_t> params; };
    std::vector<ActiveScreenEffect> m_activeScreenEffects;

    // Deferred project shader/effect reload (Issue #88) — set from a mid-frame UI
    // context, consumed at RenderFrame top where GPU-resource destruction is safe.
    std::string m_pendingShaderReloadDir;
    bool        m_hasPendingShaderReload = false;
    MaterialParamRing          m_materialRing;    // per-frame SSBO bump allocator
    glm::vec4                  m_shCoeffs[9] = {};  // stored by SetIBL
    uint32_t                   m_frameCount  = 0;   // incremented per RenderFrame

    GpuIblBake                 m_iblBake;
    GpuLtcBake                 m_ltcBake;

    std::unique_ptr<MaterialInstance>    m_defaultMaterial;
    // #106: magenta stand-in when a material ID is VALID but LoadMaterial fails
    // (unknown type / missing .samatc / JSON error) — distinguishes broken
    // references from the neutral-gray "no material assigned" default.
    std::unique_ptr<MaterialInstance>    m_errorMaterial;
    std::vector<DrawItem>                m_drawItems;

    // Spatial acceleration — built once per BuildDrawList, queried per frame.
    Core::BVHTree<entt::entity>          m_bvh;
    std::vector<entt::entity>            m_visibleEntities;   // BVH Query output
    std::vector<const DrawItem*>         m_visibleDrawItems;  // filtered per RenderFrame

    std::vector<std::unique_ptr<RenderFeature>> m_features;

    // Raw pointers into m_features — stable as long as the vector doesn't reallocate.
    // Set during Init, updated by ApplyWorldSettings on tonemap replacement.
    GBufferFeature*          m_gbufferFeature          = nullptr;
    VelocityPrepassFeature*  m_velocityPrepassFeature  = nullptr;
    DeferredLightingFeature* m_deferredLightingFeature = nullptr;
    SSRFeature*              m_ssrFeature              = nullptr;
    VolumetricFogFeature*    m_volFogFeature           = nullptr;  // Issue #49
    SkyboxFeature*           m_skyboxFeature           = nullptr;
    BloomFeature*         m_bloomFeature   = nullptr;
    SSAOFeature*          m_ssaoFeature    = nullptr;
    TAAFeature*           m_taaFeature     = nullptr;
    ForwardTransparentFeature* m_forwardTransparentFeature = nullptr;  // Issue #56
    AutoExposureFeature*  m_aeFeature      = nullptr;
    DoFFeature*           m_dofFeature     = nullptr;
    MotionBlurFeature*    m_motionBlurFeature = nullptr;
    RenderFeature*        m_tonemapFeature = nullptr;  // either TonemapFeature or LutTonemapFeature
    PostFXFeature*        m_postFxFeature  = nullptr;

    // ScreenEffect injection anchors (Issue #88), indexed by EffectInject.
    // Pre-placed in m_features at their frame positions; each runs the active
    // stack entries whose type targets that injection point. RenderFrame
    // redirects handles.hdr to an anchor's output when it ran an effect.
    ScreenEffectFeature*  m_seAnchors[4] = {};

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
    glm::mat4   m_currentViewProj = glm::mat4(1.f);  // jittered; culling + transparent sort
    // Issue #107: debug lines draw after TAA and must rasterise unjittered —
    // the jittered VP made them wobble ±0.5px with no resolve to average it.
    glm::mat4   m_currentUnjitteredViewProj = glm::mat4(1.f);

    // ── Infinite grid ─────────────────────────────────────────────────────────
    bool m_infiniteGrid = false;

    // ── Selection outline ─────────────────────────────────────────────────────
    entt::entity          m_selectionEntity      = entt::null;
    entt::entity          m_highlightEntity      = entt::null;   // Issue #102
    int32_t               m_highlightSlot        = -1;           // Issue #102
    float                 m_selectionOutlineWidth = 2.f;

    // Issue #102: ID pick state. The buffer is created lazily on first pick
    // (and on resize); the snapshot maps id-1 → (entity, submeshIndex),
    // captured at pass-build time so the result survives draw-list rebuilds.
    struct IdPickState {
        bool     pending  = false;  // request queued, pass not yet recorded
        bool     rendered = false;  // pass recorded this frame → readback after present
        uint32_t px = 0, py = 0;
        std::vector<std::pair<entt::entity, uint32_t>> snapshot;
        PickResult result{};
        bool     hasResult = false;
    };
    void ResolveIdPick();
    IdPickState           m_idPick;
    RHI::RHITextureHandle m_idBuffer;
    uint32_t              m_idBufferW = 0;
    uint32_t              m_idBufferH = 0;
    bool                  m_debugIdView = false;   // X-8 debug view
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
    RGTextureHandle m_rgLdr;
    RGTextureHandle m_rgVelocity;

    // Sun light index of the current frame (Issue #49) — see GatherLights.
    int m_sunLightIndex = -1;

    // ── Frame data helpers (private, called from RenderFrame) ─────────────────
    // outSunIndex (Issue #49): index into LightUniforms of the sun directional
    // light (first isSun=true, else first directional; -1 when none). Drives
    // lightSpaceMatrix and the volumetric fog shadowed-scattering light.
    [[nodiscard]] LightUniforms GatherLights(const Scene& scene, int* outSunIndex = nullptr) const;
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
