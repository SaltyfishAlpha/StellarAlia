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
    // Issue #84: PrevTransform stamped each frame by UpdateTransforms.
    // seeded=false → first-frame velocity forced to 0.
    m_registry.emplace<PrevTransformComponent>(e);
    m_registry.emplace<EntityIdComponent>(e, m_nextLocalId++);
    m_rootOrder.push_back(e);
    m_hierarchyDirty = true;
    return e;
}

void Scene::Clear() {
    m_registry.clear();
    m_worldSettings   = WorldSettings{};
    m_rootOrder.clear();
    m_sortedEntities.clear();
    m_hierarchyDirty  = true;
    m_materialDirty      = false;
    m_skinnedMeshDirty   = false;
    m_nextLocalId        = 1;
}

entt::entity Scene::FindBySceneLocalId(uint64_t id) const {
    if (id == 0) return entt::null;
    // Linear scan — scenes rarely exceed a few hundred entities and lookups are
    // editor-side (Inspector picker / EntityRef resolve), not per-frame hot.
    auto view = m_registry.view<EntityIdComponent>();
    for (auto e : view) {
        if (view.get<EntityIdComponent>(e).sceneLocalId == id) return e;
    }
    return entt::null;
}

void Scene::AssignSceneLocalId(entt::entity e, uint64_t id) {
    if (!m_registry.valid(e) || id == 0) return;
    m_registry.emplace_or_replace<EntityIdComponent>(e, id);
    if (id >= m_nextLocalId) m_nextLocalId = id + 1;
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

    m_rootOrder.erase(std::remove(m_rootOrder.begin(), m_rootOrder.end(), entity),
                      m_rootOrder.end());
    m_registry.destroy(entity);
    m_hierarchyDirty = true;
}

void Scene::SetParent(entt::entity child, entt::entity parent) {
    if (!m_registry.valid(child)) return;

    const bool wasRoot = [&]() -> bool {
        const auto* h = m_registry.try_get<HierarchyComponent>(child);
        return !h || h->parent == entt::null;
    }();

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

    const bool isNowRoot = (parent == entt::null);
    if (wasRoot && !isNowRoot) {
        m_rootOrder.erase(std::remove(m_rootOrder.begin(), m_rootOrder.end(), child),
                          m_rootOrder.end());
    } else if (!wasRoot && isNowRoot) {
        m_rootOrder.push_back(child);
    }

    m_hierarchyDirty = true;
    MarkDirty(child);
}

// ── Systems ───────────────────────────────────────────────────────────────────

void Scene::RebuildSortedOrder() {
    m_sortedEntities.clear();

    // BFS seeded from m_rootOrder to preserve user-defined root order.
    std::queue<entt::entity> q;
    for (entt::entity e : m_rootOrder)
        if (m_registry.valid(e))
            q.push(e);

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

        // Issue #84: capture last-frame world matrix into PrevTransform before
        // overwriting w->matrix. Only seeded entities snapshot here; fresh
        // entities are seeded at the end (prev=curr, velocity=0 first frame).
        if (auto* prev = m_registry.try_get<PrevTransformComponent>(e); prev && prev->seeded)
            prev->prevModel = w->matrix;

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

    // Issue #84: seed prev=curr for fresh entities (first frame, velocity=0).
    auto seedView = m_registry.view<WorldTransformComponent, PrevTransformComponent>();
    for (entt::entity e : seedView) {
        auto& prev = seedView.get<PrevTransformComponent>(e);
        if (!prev.seeded) {
            prev.prevModel = seedView.get<WorldTransformComponent>(e).matrix;
            prev.seeded    = true;
        }
    }
}

void Scene::EnsureWorldUpToDate(entt::entity entity) {
    if (!m_registry.valid(entity)) return;

    // Walk up to root collecting ancestors. Stack-allocated for the common
    // shallow hierarchy case; falls back to heap when depth >= 32.
    entt::entity stack[32];
    int top = 0;
    std::vector<entt::entity> overflow;
    for (entt::entity cur = entity; cur != entt::null; ) {
        if (top < 32) stack[top++] = cur;
        else          overflow.push_back(cur);
        const auto* h = m_registry.try_get<HierarchyComponent>(cur);
        cur = (h ? h->parent : entt::null);
    }

    // Walk root → leaf, recomputing any dirty link. Mirrors the body of
    // UpdateTransforms exactly (Animated > Transform priority).
    auto recompute = [&](entt::entity e) {
        auto* w = m_registry.try_get<WorldTransformComponent>(e);
        if (!w || !w->dirty) return;
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
    };

    for (auto it = overflow.rbegin(); it != overflow.rend(); ++it)
        recompute(*it);
    for (int i = top - 1; i >= 0; --i)
        recompute(stack[i]);
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

void Scene::MoveRootBefore(entt::entity entity, entt::entity before) {
    auto it = std::find(m_rootOrder.begin(), m_rootOrder.end(), entity);
    if (it == m_rootOrder.end()) return;
    m_rootOrder.erase(it);
    auto tgt = std::find(m_rootOrder.begin(), m_rootOrder.end(), before);
    m_rootOrder.insert(tgt != m_rootOrder.end() ? tgt : m_rootOrder.end(), entity);
    m_hierarchyDirty = true;
}

void Scene::MoveRootAfter(entt::entity entity, entt::entity after) {
    auto it = std::find(m_rootOrder.begin(), m_rootOrder.end(), entity);
    if (it == m_rootOrder.end()) return;
    m_rootOrder.erase(it);
    auto tgt = std::find(m_rootOrder.begin(), m_rootOrder.end(), after);
    m_rootOrder.insert(tgt != m_rootOrder.end() ? std::next(tgt) : m_rootOrder.end(), entity);
    m_hierarchyDirty = true;
}

} // namespace StellarAlia
