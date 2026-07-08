#include "ui/presenters/SceneHierarchyPresenter.hpp"

#include "EditorSelection.hpp"
#include "command/CommandManager.hpp"
#include "command/commands/EntityCommands.hpp"
#include "command/commands/ComponentCommands.hpp"
#include "function/scene/Scene.hpp"
#include "function/scene/Components.hpp"
#include "function/scene/EntityFactory.hpp"
#include "function/scene/SceneSerializer.hpp"
#include "resource/AssetRegistry.hpp"
#include "resource/loaders/ModelLoader.hpp"
#include "core/logs/Log.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <vector>

namespace fs = std::filesystem;

namespace StellarAlia::Editor {

// ── Hierarchy helpers ─────────────────────────────────────────────────────────

static bool IsInSubtree(entt::entity candidate, entt::entity subtreeRoot,
                        const entt::registry& reg) {
    if (candidate == subtreeRoot) return true;
    const auto* hc = reg.try_get<HierarchyComponent>(subtreeRoot);
    if (!hc) return false;
    for (entt::entity child : hc->children)
        if (IsInSubtree(candidate, child, reg)) return true;
    return false;
}

// Collect an entity and all its descendants (pre-order) into out. Used so an
// undo of Duplicate destroys the entire copied subtree — Scene::DestroyEntity
// orphans children rather than recursing, so the roots alone aren't enough.
static void CollectSubtree(entt::entity e, const entt::registry& reg,
                           std::vector<entt::entity>& out) {
    out.push_back(e);
    if (const auto* hc = reg.try_get<HierarchyComponent>(e))
        for (entt::entity child : hc->children)
            if (reg.valid(child)) CollectSubtree(child, reg, out);
}

static void MoveChildBefore(entt::entity child, entt::entity beforeSibling,
                             entt::registry& reg) {
    entt::entity parent = entt::null;
    if (const auto* hc = reg.try_get<HierarchyComponent>(child))
        parent = hc->parent;
    if (parent == entt::null) return;
    auto* phc = reg.try_get<HierarchyComponent>(parent);
    if (!phc) return;
    auto& ch = phc->children;
    auto it = std::find(ch.begin(), ch.end(), child);
    if (it == ch.end()) return;
    ch.erase(it);
    auto tgt = std::find(ch.begin(), ch.end(), beforeSibling);
    ch.insert(tgt != ch.end() ? tgt : ch.end(), child);
}

static void MoveChildAfter(entt::entity child, entt::entity afterSibling,
                            entt::registry& reg) {
    entt::entity parent = entt::null;
    if (const auto* hc = reg.try_get<HierarchyComponent>(child))
        parent = hc->parent;
    if (parent == entt::null) return;
    auto* phc = reg.try_get<HierarchyComponent>(parent);
    if (!phc) return;
    auto& ch = phc->children;
    auto it = std::find(ch.begin(), ch.end(), child);
    if (it == ch.end()) return;
    ch.erase(it);
    auto tgt = std::find(ch.begin(), ch.end(), afterSibling);
    ch.insert(tgt != ch.end() ? std::next(tgt) : ch.end(), child);
}

// ── Constructor ───────────────────────────────────────────────────────────────

SceneHierarchyPresenter::SceneHierarchyPresenter(EditorContext& ctx)
    : m_ctx(ctx) {}

// ── Request interface ─────────────────────────────────────────────────────────

void SceneHierarchyPresenter::RequestCreate(CreateOp::Kind kind,
                                             const fs::path& templatePath,
                                             entt::entity parent) {
    m_pendingCreate = { kind, templatePath, parent };
}

void SceneHierarchyPresenter::RequestDelete(std::vector<entt::entity> entities) {
    m_pendingDeletes = std::move(entities);
}

void SceneHierarchyPresenter::RequestDuplicate(std::vector<entt::entity> entities) {
    m_pendingDuplicates = std::move(entities);
}

void SceneHierarchyPresenter::RequestReparent(std::vector<entt::entity> ordered,
                                               entt::entity target, DnDMode mode) {
    m_pendingDnD = { std::move(ordered), target, mode, true };
}

void SceneHierarchyPresenter::RequestAssetDrop(AssetDropOp op) {
    m_pendingAssetDrop = std::move(op);
}

// ── DuplicateEntity ───────────────────────────────────────────────────────────

entt::entity SceneHierarchyPresenter::DuplicateEntity(entt::entity src) {
    Scene& scene = *m_ctx.scene;
    auto& reg = scene.Registry();

    const auto& srcTag = reg.get<TagComponent>(src);
    entt::entity dst = scene.CreateEntity(srcTag.name + " (Copy)");

    if (auto* t  = reg.try_get<TransformComponent>(src))        reg.emplace_or_replace<TransformComponent>(dst, *t);
    if (auto* c  = reg.try_get<CameraComponent>(src))           reg.emplace_or_replace<CameraComponent>(dst, *c);
    if (auto* dl = reg.try_get<DirectionalLightComponent>(src)) reg.emplace_or_replace<DirectionalLightComponent>(dst, *dl);
    if (auto* pl = reg.try_get<PointLightComponent>(src))       reg.emplace_or_replace<PointLightComponent>(dst, *pl);
    if (auto* sl = reg.try_get<SpotLightComponent>(src))        reg.emplace_or_replace<SpotLightComponent>(dst, *sl);
    if (auto* al = reg.try_get<AreaLightComponent>(src))        reg.emplace_or_replace<AreaLightComponent>(dst, *al);
    if (auto* sm = reg.try_get<StaticMeshComponent>(src))       reg.emplace_or_replace<StaticMeshComponent>(dst, *sm);
    if (auto* mr = reg.try_get<MeshRendererComponent>(src))     reg.emplace_or_replace<MeshRendererComponent>(dst, *mr);
    if (auto* mo = reg.try_get<MaterialOverrideComponent>(src)) reg.emplace_or_replace<MaterialOverrideComponent>(dst, *mo);
    if (auto* an = reg.try_get<AnimatorComponent>(src))         reg.emplace_or_replace<AnimatorComponent>(dst, *an);
    if (auto* sk = reg.try_get<SkinnedMeshComponent>(src)) {
        SkinnedMeshComponent skCopy{};
        skCopy.meshAsset     = sk->meshAsset;
        skCopy.skeletonAsset = sk->skeletonAsset;   // #83 P1
        reg.emplace_or_replace<SkinnedMeshComponent>(dst, skCopy);
        scene.MarkSkinnedMeshDirty();
    }
    if (auto* rb = reg.try_get<RigidBodyComponent>(src)) {
        RigidBodyComponent rbCopy = *rb;
        rbCopy.bodyId = ~0u;
        reg.emplace_or_replace<RigidBodyComponent>(dst, rbCopy);
    }
    if (auto* col = reg.try_get<ColliderComponent>(src)) reg.emplace_or_replace<ColliderComponent>(dst, *col);
    if (reg.any_of<StaticGeometryTag>(src))              reg.emplace_or_replace<StaticGeometryTag>(dst);

    const auto* hc = reg.try_get<HierarchyComponent>(src);
    if (hc) {
        std::vector<entt::entity> srcChildren = hc->children;
        for (entt::entity child : srcChildren) {
            entt::entity childDst = DuplicateEntity(child);
            scene.SetParent(childDst, dst);
        }
    }
    return dst;
}

// ── Update ────────────────────────────────────────────────────────────────────

void SceneHierarchyPresenter::Update(float /*dt*/) {
    Scene& scene = *m_ctx.scene;
    auto&  reg   = scene.Registry();

    // ── Create ───────────────────────────────────────────────────────────────
    if (m_pendingCreate.kind != CreateOp::None) {
        CreateOp op = std::move(m_pendingCreate);
        m_pendingCreate = {};

        if (m_ctx.cmdMgr) {
            auto created = std::make_shared<std::vector<entt::entity>>();
            const std::string desc = (op.kind == CreateOp::Empty)
                                    ? "Create Entity" : "Create From Template";
            m_ctx.cmdMgr->Execute(std::make_unique<CallbackCommand>(
                desc,
                [this, op, created](EditorContext& c){
                    created->clear();
                    Scene& s = *c.scene;
                    auto&  r = s.Registry();
                    entt::entity e = entt::null;
                    if (op.kind == CreateOp::Empty) {
                        e = s.CreateEntity("Entity");
                        if (r.valid(e)) CollectSubtree(e, r, *created);
                    } else {
                        auto spawned = SceneSerializer::SpawnFromTemplate(s, op.templatePath);
                        if (!spawned.empty()) {
                            e = spawned.front();
                            s.MarkMaterialDirty();
                            for (entt::entity sp : spawned)
                                if (r.valid(sp)) CollectSubtree(sp, r, *created);
                        }
                    }
                    if (r.valid(e)) {
                        if (r.valid(op.parent)) s.SetParent(e, op.parent);
                        if (c.selection) c.selection->SelectEntity(e);
                    }
                },
                [created](EditorContext& c){
                    auto& r = c.scene->Registry();
                    for (entt::entity e : *created)
                        if (r.valid(e)) c.scene->DestroyEntity(e);
                    created->clear();
                    if (c.selection) c.selection->Clear();
                }),
                m_ctx);
        } else {
            entt::entity e = entt::null;
            if (op.kind == CreateOp::Empty) {
                e = scene.CreateEntity("Entity");
            } else {
                auto spawned = SceneSerializer::SpawnFromTemplate(scene, op.templatePath);
                if (!spawned.empty()) {
                    e = spawned.front();
                    scene.MarkMaterialDirty();
                }
            }
            if (reg.valid(e)) {
                if (reg.valid(op.parent))
                    scene.SetParent(e, op.parent);
                m_ctx.selection->SelectEntity(e);
            }
        }
    }

    // ── Duplicate ─────────────────────────────────────────────────────────
    if (!m_pendingDuplicates.empty()) {
        std::vector<entt::entity> srcs = std::move(m_pendingDuplicates);
        if (m_ctx.cmdMgr) {
            // created = full copied subtree(s), shared between redo (fills it) and
            // undo (destroys it). Re-running redo re-duplicates with fresh ids.
            auto created = std::make_shared<std::vector<entt::entity>>();
            m_ctx.cmdMgr->Execute(std::make_unique<CallbackCommand>(
                "Duplicate Entities",
                [this, srcs, created](EditorContext& c){
                    created->clear();
                    auto& r = c.scene->Registry();
                    std::vector<entt::entity> roots;
                    for (entt::entity src : srcs) {
                        if (!r.valid(src)) continue;
                        entt::entity dst = DuplicateEntity(src);
                        roots.push_back(dst);
                        CollectSubtree(dst, r, *created);
                    }
                    if (!roots.empty()) {
                        c.scene->MarkMaterialDirty();
                        if (c.selection) c.selection->SelectEntities(roots);
                    }
                },
                [created](EditorContext& c){
                    auto& r = c.scene->Registry();
                    for (entt::entity e : *created)
                        if (r.valid(e)) c.scene->DestroyEntity(e);
                    created->clear();
                    if (c.selection) c.selection->Clear();
                }),
                m_ctx);
        } else {
            std::vector<entt::entity> dsts;
            dsts.reserve(srcs.size());
            for (entt::entity src : srcs)
                if (reg.valid(src)) dsts.push_back(DuplicateEntity(src));
            if (!dsts.empty()) {
                scene.MarkMaterialDirty();
                m_ctx.selection->SelectEntities(dsts);
            }
        }
    }

    // ── Delete ────────────────────────────────────────────────────────────
    if (!m_pendingDeletes.empty()) {
        std::vector<entt::entity> es = std::move(m_pendingDeletes);
        for (entt::entity e : es) {
            if (reg.valid(e))
                scene.DestroyEntity(e);
        }
        m_ctx.selection->Clear();
    }

    // ── Asset drop ────────────────────────────────────────────────────────
    if (m_pendingAssetDrop.valid) {
        const fs::path   assetPath = std::move(m_pendingAssetDrop.assetPath);
        const entt::entity parent  = m_pendingAssetDrop.parent;
        const glm::vec3  spawnPos  = m_pendingAssetDrop.spawnPos;
        const glm::quat  spawnRot  = m_pendingAssetDrop.spawnRot;
        m_pendingAssetDrop = {};

        std::string ext = assetPath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c){ return static_cast<char>(::tolower(c)); });

        if (ext == ".sascene") {
            if (m_ctx.onSceneLoad) m_ctx.onSceneLoad(assetPath);
        } else if (Resource::ModelLoader::SupportsExtension(ext)) {
            if (!m_ctx.assetReg) {
                SA_LOG_WARN("SceneHierarchyPresenter: no registry — cannot instantiate mesh");
            } else {
                const Resource::AssetEntry* entry = m_ctx.assetReg->FindBySourcePath(assetPath);
                if (!entry || !entry->id.IsValid()) {
                    SA_LOG_WARN("SceneHierarchyPresenter: '{}' not in registry — import it first",
                                assetPath.filename().string());
                } else {
                    if (m_ctx.cmdMgr) {
                        m_ctx.cmdMgr->Execute(
                            std::make_unique<CreateStaticMeshCommand>(
                                assetPath.stem().string(), entry->id, parent,
                                spawnPos, spawnRot),
                            m_ctx);
                    } else {
                        entt::entity e = EntityFactory::CreateStaticMesh(
                            scene, assetPath.stem().string(), entry->id,
                            spawnPos, spawnRot);
                        if (reg.valid(parent))
                            scene.SetParent(e, parent);
                        scene.MarkMaterialDirty();
                        m_ctx.selection->SelectEntity(e);
                    }
                }
            }
        }
    }

    // ── Drag-and-drop reparent ────────────────────────────────────────────
    if (m_pendingDnD.valid) {
        DnDOp op = std::move(m_pendingDnD);
        m_pendingDnD = {};

        if (op.target == entt::null) {
            for (entt::entity dragged : op.ordered) {
                if (!reg.valid(dragged)) continue;
                entt::entity oldParent = entt::null;
                if (const auto* hc = reg.try_get<HierarchyComponent>(dragged))
                    oldParent = hc->parent;
                if (m_ctx.cmdMgr && oldParent != entt::null) {
                    m_ctx.cmdMgr->Execute(
                        std::make_unique<ReparentEntityCommand>(dragged, oldParent, entt::null),
                        m_ctx);
                } else {
                    scene.SetParent(dragged, entt::null);
                }
            }
        } else if (reg.valid(op.target)) {
            if (op.mode == DnDMode::AsChild) {
                for (entt::entity dragged : op.ordered) {
                    if (!reg.valid(dragged) || dragged == op.target) continue;
                    if (IsInSubtree(op.target, dragged, reg)) continue;
                    entt::entity oldParent = entt::null;
                    if (const auto* hc = reg.try_get<HierarchyComponent>(dragged))
                        oldParent = hc->parent;
                    if (m_ctx.cmdMgr) {
                        m_ctx.cmdMgr->Execute(
                            std::make_unique<ReparentEntityCommand>(dragged, oldParent, op.target),
                            m_ctx);
                    } else {
                        scene.SetParent(dragged, op.target);
                    }
                }
            } else {
                // BeforeSibling / AfterSibling — ordered list is in visual draw order.
                // Reverse for AfterSibling so each insert lands after the target.
                std::vector<entt::entity> sorted = op.ordered;
                if (op.mode == DnDMode::AfterSibling)
                    std::reverse(sorted.begin(), sorted.end());

                entt::entity targetParent = entt::null;
                if (const auto* hc = reg.try_get<HierarchyComponent>(op.target))
                    targetParent = hc->parent;

                for (entt::entity dragged : sorted) {
                    if (!reg.valid(dragged) || dragged == op.target) continue;
                    if (targetParent == entt::null) {
                        scene.SetParent(dragged, entt::null);
                        if (op.mode == DnDMode::BeforeSibling)
                            scene.MoveRootBefore(dragged, op.target);
                        else
                            scene.MoveRootAfter(dragged, op.target);
                    } else {
                        if (IsInSubtree(targetParent, dragged, reg)) continue;
                        scene.SetParent(dragged, targetParent);
                        if (op.mode == DnDMode::BeforeSibling)
                            MoveChildBefore(dragged, op.target, reg);
                        else
                            MoveChildAfter(dragged, op.target, reg);
                    }
                }
            }
        }
    }
}

} // namespace StellarAlia::Editor
