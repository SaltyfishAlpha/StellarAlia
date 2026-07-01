#pragma once

#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "core/asset/AssetID.hpp"
#include "function/scene/Components.hpp"

namespace StellarAlia {

// ─────────────────────────────────────────────────────────────────────────────
// ColorGradingSettings — parametric color grading applied post-ACES.
//
// Parameters are baked into a 32³ RGBA16F 3D LUT by a compute shader whenever
// they change.  The LUT is then sampled once per pixel in the Tonemap pass.
// Only active when PostProcessSettings::tonemapMode == Builtin.
// ─────────────────────────────────────────────────────────────────────────────
struct ColorGradingSettings {
    bool      enabled    = false;
    glm::vec3 lift       = {0.f, 0.f, 0.f};  // shadow additive offset  [-0.5, 0.5]
    glm::vec3 midtone    = {1.f, 1.f, 1.f};  // midtone power (ASC CDL) [ 0.1, 3.0]
    glm::vec3 gain       = {1.f, 1.f, 1.f};  // highlight multiplier    [ 0.0, 3.0]
    float     saturation = 1.f;              // [0, 3]
    float     contrast   = 1.f;             // [0, 3], pivot at 0.5
};

// ─────────────────────────────────────────────────────────────────────────────
// ScreenEffectInstance (Issue #88) — one entry in the per-scene custom
// post-process stack. Mirrors Unity's Volume Override / UE's Post-Process
// Blendable: an effect runs only if it appears here. `name` references a cooked
// ScreenEffectType (@Effect); `params` holds per-instance @Param overrides by
// name (missing keys fall back to the type's defaults). Order within the list =
// execution order inside each injection point.
// ─────────────────────────────────────────────────────────────────────────────
struct ScreenEffectInstance {
    std::string                       name;
    bool                              enabled = true;
    std::map<std::string, ParamValue> params;
};

// ─────────────────────────────────────────────────────────────────────────────
// PostProcessSettings — runtime post-process parameters.
//
// Embedded in WorldSettings::pp.  All fields have sensible defaults.
// Serialized as a "postProcess" sub-object inside the "world" block.
// ─────────────────────────────────────────────────────────────────────────────
struct PostProcessSettings {
    // ── Bloom ─────────────────────────────────────────────────────────────────
    bool  bloomEnabled   = true;
    float bloomThreshold = 1.0f;  // luminance cutoff; knee = threshold * 0.1
    float bloomStrength  = 0.4f;  // composite blend weight
    float bloomRadius    = 1.0f;  // widest upsample radius; each level scales by 0.85
    int   bloomMipLevels = 3;     // pyramid depth [2, 8]; changing triggers desc set rebuild

    // ── Tonemap ───────────────────────────────────────────────────────────────
    enum class TonemapMode { Builtin, LUT };
    TonemapMode tonemapMode  = TonemapMode::Builtin;
    AssetID     tonemapLut;       // only used when tonemapMode == LUT
    float       exposure     = 1.f;
    float       lutStrength  = 1.f;

    // ── Color Grading (Builtin tonemap mode only) ─────────────────────────────
    ColorGradingSettings colorGrading;

    // ── SSAO (GTAO) ───────────────────────────────────────────────────────────
    bool  ssaoEnabled       = false;
    float ssaoRadius        = 32.f;   // sampling radius in pixels
    float ssaoStrength      = 1.0f;
    float ssaoBias          = 0.025f; // sin-domain bias against self-occlusion
    int   ssaoDirections    = 4;
    int   ssaoSteps         = 3;
    float ssaoBlurSharpness = 10.f;

    // ── TAA (Temporal Anti-Aliasing) ─────────────────────────────────────────
    bool  taaEnabled       = false;
    float taaBlendStatic   = 0.1f;  // history weight in static regions (smaller = more stable)
    float taaBlendMotion   = 0.5f;  // history weight in motion regions
    bool  taaAntiGhosting  = true;  // enable 3x3 neighborhood clamping

    // ── Auto Exposure (eye adaptation) ──────────────────────────────────────
    bool  autoExposureEnabled = false;
    float aeEvMin        = -4.0f;   // log2 luminance range minimum (EV)
    float aeEvMax        =  4.0f;   // log2 luminance range maximum (EV)
    float aeAdaptSpeed   =  2.0f;   // adaptation speed (1/sec)
    float aeLowPercent   =  0.45f;  // histogram low-cut percentile
    float aeHighPercent  =  0.95f;  // histogram high-cut percentile

    // ── Depth of Field (DoF) ─────────────────────────────────────────────────
    bool  dofEnabled      = false;
    float focusDistance   = 5.0f;   // view-space focus plane distance (meters)
    float aperture        = 1.4f;   // f-number (lower = shallower DoF)
    float focalLength     = 50.0f;  // mm (affects CoC scale with aperture)
    int   dofSamples      = 16;     // blur kernel sample count [4, 32]
    bool  autoFocus       = false;  // Phase 2: GPU depth readback
    float maxCocPx        = 20.0f;  // max CoC radius in pixels

    // ── Motion Blur (Camera Mode, Issue #46 Phase 1) ─────────────────────────
    // Reconstructs per-pixel velocity from depth + prevViewProj; covers camera
    // pan/dolly/rotation. Per-object velocity (skinned mesh swing, moving rigid
    // body under static camera) is Phase 2 work.
    bool  motionBlurEnabled  = false;
    float motionBlurStrength = 0.5f;   // [0..2] — 1.0 ~ physically correct
    int   motionBlurSamples  = 8;      // [4..32] reconstruct sample count
    float motionBlurMaxSpeed = 0.1f;   // NDC screen-space velocity clamp

    // ── Screen modifications (Issue #47) ─────────────────────────────────────
    // PostFX pass runs after Tonemap on the LDR buffer; each layer is
    // independently toggleable. All three disabled → pure LDR→swapchain copy.
    bool  vignetteEnabled     = false;
    float vignetteIntensity   = 0.4f;   // [0..1] elliptical falloff start
    float vignetteSmoothness  = 0.6f;   // [0.01..1] falloff width
    bool  caEnabled           = false;
    float caStrength          = 0.5f;   // [0..5] radial RGB offset multiplier
    bool  filmGrainEnabled    = false;
    float filmGrainIntensity  = 0.1f;   // [0..0.3] amplitude
    float filmGrainSize       = 1.6f;   // [0.5..5] noise tile scale

    // ── Screen Space Reflections (Issue #48) ─────────────────────────────────
    // Compute pass after DeferredLighting, before TAA; replaces the IBL
    // env-probe specular term with screen-traced colour where confident.
    // Relies on TAA (#42) to denoise the single-bounce ray-march jitter.
    bool  ssrEnabled      = false;
    float ssrMaxRoughness = 0.4f;   // fade SSR out above this roughness (fall back to IBL)
    int   ssrMaxSteps     = 64;     // [16..128] linear march sample count
    float ssrThickness    = 0.1f;   // view-space depth tolerance for hit test
    float ssrStrength     = 1.0f;   // [0..1] reflection blend weight

    // ── Screen Effects (Issue #88) — ordered per-scene custom post-process stack ──
    // Each entry references a cooked .saeffect; only listed effects run. Grouped
    // by injection point at execution time (see EffectInject).
    std::vector<ScreenEffectInstance> screenEffects;
};

// ─────────────────────────────────────────────────────────────────────────────
// WorldSettings
//
// Scene-level global configuration that does not belong to any individual
// entity.  Serialized as a top-level "world" key in .sascene JSON.
// ─────────────────────────────────────────────────────────────────────────────
struct WorldSettings {
    // ── Background ────────────────────────────────────────────────────────────
    enum class BackgroundMode { SolidColor, Skybox };
    BackgroundMode backgroundMode  = BackgroundMode::SolidColor;
    glm::vec3      backgroundColor = { 0.08f, 0.08f, 0.08f };  // linear HDR

    // Source HDR panorama (.satex, RGBA32F equirect).  Read-only input for GpuIblBake.
    AssetID skyboxHdr;

    // Offline-baked IBL products (GPU-computed, cached as .satex/.sash9).
    // When all four are valid and cached, the render loop skips GpuIblBake entirely.
    AssetID sh9;              // L0+L1+L2 SH coefficients (.sash9)
    AssetID prefilteredEnv;   // GGX prefiltered specular cubemap (mip chain, .satex)
    AssetID brdfLut;          // Split-sum BRDF LUT 2D (fixed UUID, .satex)
    AssetID skyboxCubemap;    // Equirect HDR converted to cubemap (.satex)

    // ── Post-process ─────────────────────────────────────────────────────────
    PostProcessSettings pp;
};

// ─────────────────────────────────────────────────────────────────────────────
// Scene
//
// Thin wrapper around entt::registry.  Manages entity lifetime, hierarchy
// bookkeeping, and per-frame world-transform propagation.
//
// Usage pattern:
//   Scene scene("MyLevel");
//   entt::entity cam = scene.CreateEntity("MainCamera");
//   scene.Registry().emplace<CameraComponent>(cam);
//   // ...per frame:
//   scene.UpdateTransforms();
// ─────────────────────────────────────────────────────────────────────────────
class Scene {
public:
    explicit Scene(std::string name = "Untitled");

    // ── Entity management ─────────────────────────────────────────────────

    // Creates an entity with TagComponent, TransformComponent, and
    // WorldTransformComponent pre-attached.
    entt::entity CreateEntity(std::string_view name = "Entity");

    // Destroys entity: detaches from parent, orphans children, then removes.
    void DestroyEntity(entt::entity entity);

    // Destroys all entities and resets world settings to defaults.
    // Call before loading a new scene file.
    void Clear();

    // Sets 'child' as a child of 'parent' (entt::null = detach from parent).
    // Maintains HierarchyComponent on both entities.
    void SetParent(entt::entity child, entt::entity parent);

    // ── Registry access ───────────────────────────────────────────────────

    entt::registry&       Registry()       { return m_registry; }
    const entt::registry& Registry() const { return m_registry; }

    // Convenience view shorthand:
    //   for (auto [e, mesh, world] : scene.View<StaticMeshComponent, WorldTransformComponent>().each())
    template<typename... Cs>
    auto View() { return m_registry.view<Cs...>(); }

    template<typename... Cs>
    auto View() const { return m_registry.view<Cs...>(); }

    // ── Metadata ──────────────────────────────────────────────────────────

    [[nodiscard]] const std::string& GetName() const { return m_name; }
    void SetName(std::string name) { m_name = std::move(name); }

    [[nodiscard]] const WorldSettings& GetWorldSettings() const { return m_worldSettings; }
    WorldSettings& GetWorldSettings() { return m_worldSettings; }

    // Returns root entities in user-defined order.  Use this for hierarchy
    // display and serialization instead of reg.view<TagComponent>().
    [[nodiscard]] const std::vector<entt::entity>& GetRootOrder() const { return m_rootOrder; }

    // Reorder root entities.  entity must already be a root.
    void MoveRootBefore(entt::entity entity, entt::entity before);
    void MoveRootAfter (entt::entity entity, entt::entity after);

    // ── Systems ───────────────────────────────────────────────────────────

    // Recomputes WorldTransformComponent for every dirty entity.
    // Rebuilds the topological traversal order (BFS from roots) whenever the
    // hierarchy has changed since the last call.  Call once per frame.
    void UpdateTransforms();

    // Walks the parent chain from `entity` up to root, then recomputes
    // WorldTransformComponent top-down for any node whose dirty flag is true.
    // Used by the script API world-space accessors so they see a fresh matrix
    // even when called between the physics tick and the per-frame
    // UpdateTransforms pass. Cost: O(depth) — bounded by hierarchy depth.
    void EnsureWorldUpToDate(entt::entity entity);

    // Mark an entity's world transform (and all descendants) as dirty.
    // Call after manually modifying a TransformComponent.
    void MarkDirty(entt::entity entity);

    // Signal that a material-override component was edited and the renderer's
    // draw-list needs to be rebuilt before the next frame.
    void MarkMaterialDirty()           { m_materialDirty      = true; }
    bool IsAndClearMaterialDirty()     { bool v = m_materialDirty;      m_materialDirty      = false; return v; }

    // Signal that a SkinnedMeshComponent's meshAsset was changed in the editor
    // and AnimationSystem needs to re-prepare the entity before the next frame.
    void MarkSkinnedMeshDirty()        { m_skinnedMeshDirty   = true; }
    bool IsAndClearSkinnedMeshDirty()  { bool v = m_skinnedMeshDirty;  m_skinnedMeshDirty   = false; return v; }

    // ── Stable scene-local identity (#75) ────────────────────────────────────
    //
    // Each entity gets an EntityIdComponent with a monotonically-increasing
    // sceneLocalId at creation time; the ID survives save/load and is used by
    // script-field `EntityRef` to persist cross-entity references.

    // Look up an entity by its sceneLocalId; entt::null when not found.
    entt::entity FindBySceneLocalId(uint64_t id) const;
    // Reserve an explicit sceneLocalId for an entity (used by SceneSerializer
    // load to restore IDs from disk before further entity creation).
    void          AssignSceneLocalId(entt::entity e, uint64_t id);

private:
    std::string    m_name;
    WorldSettings  m_worldSettings;
    entt::registry m_registry;

    // User-defined root order.  Maintained by CreateEntity / DestroyEntity / SetParent / Clear.
    std::vector<entt::entity> m_rootOrder;

    // Cached BFS traversal order — parents always appear before their children.
    // Rebuilt lazily whenever m_hierarchyDirty is set.
    std::vector<entt::entity> m_sortedEntities;
    bool                      m_hierarchyDirty = true;
    bool                      m_materialDirty      = false;
    bool                      m_skinnedMeshDirty   = false;

    // Monotonic counter for EntityIdComponent.sceneLocalId; starts at 1
    // (0 = "unassigned" sentinel). Reset on Clear().
    uint64_t                  m_nextLocalId = 1;

    void RebuildSortedOrder();
    void MarkDirtyRecursive(entt::entity entity);
};

} // namespace StellarAlia
