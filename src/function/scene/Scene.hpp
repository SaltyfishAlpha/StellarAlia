#pragma once

#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "core/asset/AssetID.hpp"
#include "function/scene/Components.hpp"

namespace StellarAlia {

// ─────────────────────────────────────────────────────────────────────────────
// WorldSettings
//
// Scene-level global configuration that does not belong to any individual
// entity.  Serialized as a top-level "world" key in .sascene JSON.
// ─────────────────────────────────────────────────────────────────────────────
struct WorldSettings {
    // Source HDR panorama (.satex, RGBA32F equirect).  Read-only input for GpuIblBake.
    AssetID skyboxHdr;

    // Offline-baked IBL products (GPU-computed, cached as .satex/.sash9).
    // When all four are valid and cached, the render loop skips GpuIblBake entirely.
    AssetID sh9;              // L0+L1+L2 SH coefficients (.sash9)
    AssetID prefilteredEnv;   // GGX prefiltered specular cubemap (mip chain, .satex)
    AssetID brdfLut;          // Split-sum BRDF LUT 2D (fixed UUID, .satex)
    AssetID skyboxCubemap;    // Equirect HDR converted to cubemap (.satex)
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
//   scene.Registry().emplace<ActiveCameraTag>(cam);
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

    // ── Systems ───────────────────────────────────────────────────────────

    // Recomputes WorldTransformComponent for every dirty entity.
    // Rebuilds the topological traversal order (BFS from roots) whenever the
    // hierarchy has changed since the last call.  Call once per frame.
    void UpdateTransforms();

    // Mark an entity's world transform (and all descendants) as dirty.
    // Call after manually modifying a TransformComponent.
    void MarkDirty(entt::entity entity);

private:
    std::string    m_name;
    WorldSettings  m_worldSettings;
    entt::registry m_registry;

    // Cached BFS traversal order — parents always appear before their children.
    // Rebuilt lazily whenever m_hierarchyDirty is set.
    std::vector<entt::entity> m_sortedEntities;
    bool                      m_hierarchyDirty = true;

    void RebuildSortedOrder();
    void MarkDirtyRecursive(entt::entity entity);
};

} // namespace StellarAlia
