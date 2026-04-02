#include "function/scene/Scene.hpp"
#include "core/logs/Log.hpp"

#include <algorithm>
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
    return e;
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

    MarkDirty(child);
}

// ── Systems ───────────────────────────────────────────────────────────────────

void Scene::UpdateTransforms() {
    // Pass 1: update root entities (no parent or parent == null)
    for (auto [entity, t, w] :
         m_registry.view<TransformComponent, WorldTransformComponent>().each()) {

        auto* h = m_registry.try_get<HierarchyComponent>(entity);
        const bool isRoot = (!h || h->parent == entt::null);
        if (!isRoot) continue;

        if (w.dirty) {
            w.matrix = LocalMatrix(t);
            w.dirty  = false;
            if (h) {
                for (entt::entity child : h->children)
                    PropagateTransform(child, w.matrix);
            }
        }
    }
}

void Scene::PropagateTransform(entt::entity entity, const glm::mat4& parentWorld) {
    if (!m_registry.valid(entity)) return;

    auto* t = m_registry.try_get<TransformComponent>(entity);
    auto* w = m_registry.try_get<WorldTransformComponent>(entity);
    if (!t || !w) return;

    w->matrix = parentWorld * LocalMatrix(*t);
    w->dirty  = false;

    if (auto* h = m_registry.try_get<HierarchyComponent>(entity)) {
        for (entt::entity child : h->children)
            PropagateTransform(child, w->matrix);
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
