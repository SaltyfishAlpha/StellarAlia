#pragma once

#include <string>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "function/scene/Components.hpp"

namespace StellarAlia {

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

    // ── Systems ───────────────────────────────────────────────────────────

    // Recomputes WorldTransformComponent for every entity whose local
    // transform has been marked dirty (dirty = true on TransformComponent write).
    // Call once per frame before rendering.
    void UpdateTransforms();

    // Mark an entity's world transform (and all descendants) as dirty.
    // Call after manually modifying a TransformComponent.
    void MarkDirty(entt::entity entity);

private:
    std::string    m_name;
    entt::registry m_registry;

    void PropagateTransform(entt::entity entity, const glm::mat4& parentWorld);
    void MarkDirtyRecursive(entt::entity entity);
};

} // namespace StellarAlia
