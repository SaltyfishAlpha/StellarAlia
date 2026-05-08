#include "function/scene/Scene.hpp"
#include "core/logs/Log.hpp"

#include <algorithm>
#include <queue>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace StellarAlia {

// ── Helpers ───────────────────────────────────────────────────────────────────

static glm::mat4 LocalMatrix(const TransformComponent& t) {
    return glm::translate(glm::mat4(1.f), t.position)
         * glm::mat4_cast(t.rotation)
         * glm::scale(glm::mat4(1.f), t.scale);
}

// ── Construction ──────────────────────────────────────────────────────────────

Scene::Scene(std::string name) : m_name(std::move(name)) {}

// ── Entity management ─────────────────────────────────────────────────────────

entt::entity Scene::CreateEntity(std::string_view name) {
    entt::entity e = m_registry.create();
    m_registry.emplace<TagComponent>(e, std::string(name));
    m_registry.emplace<TransformComponent>(e);
    m_registry.emplace<WorldTransformComponent>(e);
    m_hierarchyDirty = true;
    return e;
}

void Scene::Clear() {
    m_registry.clear();
    m_worldSettings   = WorldSettings{};
    m_sortedEntities.clear();
    m_hierarchyDirty  = true;
    m_materialDirty      = false;
    m_skinnedMeshDirty   = false;
}

void Scene::DestroyEntity(entt::entity entity) {
    if (!m_registry.valid(entity)) return;

    if (auto* h = m_registry.try_get<HierarchyComponent>(entity)) {
        // Detach from parent's children list
        if (h->parent != entt::null && m_registry.valid(h->parent)) {
            if (auto* ph = m_registry.try_get<HierarchyComponent>(h->parent)) {
                auto& siblings = ph->children;
                siblings.erase(std::remove(siblings.begin(), siblings.end(), entity),
                               siblings.end());
            }
        }
        // Orphan children (do not recurse-destroy; let caller decide)
        for (entt::entity child : h->children) {
            if (m_registry.valid(child)) {
                if (auto* ch = m_registry.try_get<HierarchyComponent>(child))
                    ch->parent = entt::null;
            }
        }
    }

    m_registry.destroy(entity);
    m_hierarchyDirty = true;
}

void Scene::SetParent(entt::entity child, entt::entity parent) {
    if (!m_registry.valid(child)) return;

    // Detach from current parent first
    if (auto* h = m_registry.try_get<HierarchyComponent>(child)) {
        if (h->parent != entt::null && h->parent != parent &&
            m_registry.valid(h->parent)) {
            if (auto* ph = m_registry.try_get<HierarchyComponent>(h->parent)) {
                auto& siblings = ph->children;
                siblings.erase(std::remove(siblings.begin(), siblings.end(), child),
                               siblings.end());
            }
        }
    }

    auto& ch = m_registry.get_or_emplace<HierarchyComponent>(child);
    ch.parent = parent;

    if (parent != entt::null && m_registry.valid(parent)) {
        auto& ph = m_registry.get_or_emplace<HierarchyComponent>(parent);
        if (std::find(ph.children.begin(), ph.children.end(), child) == ph.children.end())
            ph.children.push_back(child);
    }

    m_hierarchyDirty = true;
    MarkDirty(child);
}

// ── Systems ───────────────────────────────────────────────────────────────────

void Scene::RebuildSortedOrder() {
    m_sortedEntities.clear();

    // BFS from all root entities (no HierarchyComponent, or parent == null).
    std::queue<entt::entity> q;
    for (entt::entity e : m_registry.view<TransformComponent>()) {
        const auto* h = m_registry.try_get<HierarchyComponent>(e);
        if (!h || h->parent == entt::null)
            q.push(e);
    }

    while (!q.empty()) {
        entt::entity e = q.front();
        q.pop();
        m_sortedEntities.push_back(e);
        if (const auto* h = m_registry.try_get<HierarchyComponent>(e)) {
            for (entt::entity child : h->children)
                if (m_registry.valid(child))
                    q.push(child);
        }
    }

    m_hierarchyDirty = false;
}

void Scene::UpdateTransforms() {
    if (m_hierarchyDirty)
        RebuildSortedOrder();

    for (entt::entity e : m_sortedEntities) {
        if (!m_registry.valid(e)) continue;

        auto* w = m_registry.try_get<WorldTransformComponent>(e);
        if (!w || !w->dirty) continue;

        // Prefer AnimatedTransformComponent when the animation system has set it.
        const glm::mat4 local = [&]() -> glm::mat4 {
            if (const auto* a = m_registry.try_get<AnimatedTransformComponent>(e))
                return glm::translate(glm::mat4(1.f), a->position)
                     * glm::mat4_cast(a->rotation)
                     * glm::scale(glm::mat4(1.f), a->scale);
            if (const auto* t = m_registry.try_get<TransformComponent>(e))
                return LocalMatrix(*t);
            return glm::mat4(1.f);
        }();

        const auto* h = m_registry.try_get<HierarchyComponent>(e);
        if (!h || h->parent == entt::null) {
            w->matrix = local;
        } else {
            const auto* pw = m_registry.try_get<WorldTransformComponent>(h->parent);
            w->matrix = (pw ? pw->matrix : glm::mat4(1.f)) * local;
        }
        w->dirty = false;
    }
}

void Scene::MarkDirty(entt::entity entity) {
    MarkDirtyRecursive(entity);
}

void Scene::MarkDirtyRecursive(entt::entity entity) {
    if (!m_registry.valid(entity)) return;
    if (auto* w = m_registry.try_get<WorldTransformComponent>(entity))
        w->dirty = true;
    if (auto* h = m_registry.try_get<HierarchyComponent>(entity)) {
        for (entt::entity child : h->children)
            MarkDirtyRecursive(child);
    }
}

} // namespace StellarAlia
