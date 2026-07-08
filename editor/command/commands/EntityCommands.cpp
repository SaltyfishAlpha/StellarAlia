#include "EntityCommands.hpp"

#include "EditorContext.hpp"
#include "EditorSelection.hpp"
#include "function/scene/Scene.hpp"
#include "function/scene/EntityFactory.hpp"
#include "resource/ResourceManager.hpp"

namespace StellarAlia::Editor {

// ── DeleteEntityCommand ───────────────────────────────────────────────────────

DeleteEntityCommand::DeleteEntityCommand(entt::entity entity)
    : m_entity(entity) {}

void DeleteEntityCommand::CaptureSnapshot(const entt::registry& reg) {
    const auto& tag   = reg.get<TagComponent>(m_entity);
    m_snapshot.name   = tag.name;

    if (const auto* hc = reg.try_get<HierarchyComponent>(m_entity)) {
        m_snapshot.parent = hc->parent;
        // Non-undoable when the entity has children — orphan restoration is complex
        if (!hc->children.empty()) m_undoable = false;
    }

    // Non-undoable for complex animated/skinned entities
    if (reg.any_of<AnimatorComponent, SkinnedMeshComponent>(m_entity))
        m_undoable = false;

    if (const auto* c = reg.try_get<TransformComponent>(m_entity))        m_snapshot.transform    = *c;
    if (const auto* c = reg.try_get<CameraComponent>(m_entity))           m_snapshot.camera       = *c;
    if (const auto* c = reg.try_get<DirectionalLightComponent>(m_entity)) m_snapshot.dirLight     = *c;
    if (const auto* c = reg.try_get<PointLightComponent>(m_entity))       m_snapshot.pointLight   = *c;
    if (const auto* c = reg.try_get<SpotLightComponent>(m_entity))        m_snapshot.spotLight    = *c;
    if (const auto* c = reg.try_get<AreaLightComponent>(m_entity))        m_snapshot.areaLight    = *c;
    if (const auto* c = reg.try_get<StaticMeshComponent>(m_entity))       m_snapshot.staticMesh   = *c;
    if (const auto* c = reg.try_get<MeshRendererComponent>(m_entity))     m_snapshot.meshRenderer = *c;
    if (const auto* c = reg.try_get<MaterialOverrideComponent>(m_entity)) m_snapshot.matOverride  = *c;
    if (const auto* c = reg.try_get<RigidBodyComponent>(m_entity)) {
        RigidBodyComponent rbCopy = *c;
        rbCopy.bodyId = ~0u;  // physics handle is invalid after recreate
        m_snapshot.rigidBody = rbCopy;
    }
    if (const auto* c = reg.try_get<ColliderComponent>(m_entity))         m_snapshot.collider     = *c;
    m_snapshot.staticGeomTag = reg.any_of<StaticGeometryTag>(m_entity);
}

void DeleteEntityCommand::ApplySnapshot(EditorContext& ctx) {
    Scene& scene = *ctx.scene;
    entt::entity e = scene.CreateEntity(m_snapshot.name);
    auto& reg = scene.Registry();

    if (m_snapshot.transform)    reg.emplace_or_replace<TransformComponent>(e, *m_snapshot.transform);
    if (m_snapshot.camera)       reg.emplace_or_replace<CameraComponent>(e, *m_snapshot.camera);
    if (m_snapshot.dirLight)     reg.emplace_or_replace<DirectionalLightComponent>(e, *m_snapshot.dirLight);
    if (m_snapshot.pointLight)   reg.emplace_or_replace<PointLightComponent>(e, *m_snapshot.pointLight);
    if (m_snapshot.spotLight)    reg.emplace_or_replace<SpotLightComponent>(e, *m_snapshot.spotLight);
    if (m_snapshot.areaLight)    reg.emplace_or_replace<AreaLightComponent>(e, *m_snapshot.areaLight);
    if (m_snapshot.staticMesh)   reg.emplace_or_replace<StaticMeshComponent>(e, *m_snapshot.staticMesh);
    if (m_snapshot.meshRenderer) reg.emplace_or_replace<MeshRendererComponent>(e, *m_snapshot.meshRenderer);
    if (m_snapshot.matOverride)  reg.emplace_or_replace<MaterialOverrideComponent>(e, *m_snapshot.matOverride);
    if (m_snapshot.rigidBody)    reg.emplace_or_replace<RigidBodyComponent>(e, *m_snapshot.rigidBody);
    if (m_snapshot.collider)     reg.emplace_or_replace<ColliderComponent>(e, *m_snapshot.collider);
    if (m_snapshot.staticGeomTag) reg.emplace_or_replace<StaticGeometryTag>(e);

    if (reg.valid(m_snapshot.parent))
        scene.SetParent(e, m_snapshot.parent);

    scene.MarkMaterialDirty();
    m_entity = e;

    if (ctx.selection) ctx.selection->SelectEntity(e);
}

void DeleteEntityCommand::Execute(EditorContext& ctx) {
    auto& reg = *ctx.registry;
    if (!reg.valid(m_entity)) return;
    if (!m_snapped) {
        CaptureSnapshot(reg);
        m_snapped = true;
    }
    ctx.scene->DestroyEntity(m_entity);
    m_entity = entt::null;
    if (ctx.selection) ctx.selection->Clear();
}

void DeleteEntityCommand::Undo(EditorContext& ctx) {
    if (!m_undoable) return;
    ApplySnapshot(ctx);
}

std::string DeleteEntityCommand::GetDescription() const {
    return "Delete \"" + m_snapshot.name + "\"";
}

// ── RenameEntityCommand ───────────────────────────────────────────────────────

RenameEntityCommand::RenameEntityCommand(entt::entity entity,
                                         std::string oldName,
                                         std::string newName)
    : m_entity(entity), m_oldName(std::move(oldName)), m_newName(std::move(newName)) {}

void RenameEntityCommand::Execute(EditorContext& ctx) {
    auto& reg = *ctx.registry;
    if (!reg.valid(m_entity)) return;
    reg.get<TagComponent>(m_entity).name = m_newName;
}

void RenameEntityCommand::Undo(EditorContext& ctx) {
    auto& reg = *ctx.registry;
    if (!reg.valid(m_entity)) return;
    reg.get<TagComponent>(m_entity).name = m_oldName;
}

std::string RenameEntityCommand::GetDescription() const {
    return "Rename \"" + m_oldName + "\" to \"" + m_newName + "\"";
}

// ── ReparentEntityCommand ─────────────────────────────────────────────────────

ReparentEntityCommand::ReparentEntityCommand(entt::entity entity,
                                             entt::entity oldParent,
                                             entt::entity newParent)
    : m_entity(entity), m_oldParent(oldParent), m_newParent(newParent) {}

void ReparentEntityCommand::Execute(EditorContext& ctx) {
    auto& reg = *ctx.registry;
    if (!reg.valid(m_entity)) return;
    ctx.scene->SetParent(m_entity, m_newParent);
}

void ReparentEntityCommand::Undo(EditorContext& ctx) {
    auto& reg = *ctx.registry;
    if (!reg.valid(m_entity)) return;
    ctx.scene->SetParent(m_entity, m_oldParent);
}

std::string ReparentEntityCommand::GetDescription() const {
    return "Reparent entity";
}

// ── CreateStaticMeshCommand ───────────────────────────────────────────────────

CreateStaticMeshCommand::CreateStaticMeshCommand(std::string name, AssetID assetId,
                                                 entt::entity parent, glm::vec3 spawnPos,
                                                 glm::quat spawnRot)
    : m_name(std::move(name)), m_assetId(assetId), m_parent(parent),
      m_spawnPos(spawnPos), m_spawnRot(spawnRot) {}

void CreateStaticMeshCommand::Execute(EditorContext& ctx) {
    Scene& scene = *ctx.scene;

    // Issue #108: skinned assets (VRM, rigged FBX) get the skinned component
    // set right away — a StaticMesh entity would render the bind pose and
    // invite a conflicting hand-added SkinnedMeshComponent on top.
    const Resource::GPUMesh* gpuMesh =
        ctx.resMgr ? ctx.resMgr->LoadMesh(m_assetId) : nullptr;
    if (gpuMesh && gpuMesh->IsSkinned())
        m_entity = EntityFactory::CreateSkinnedMesh(scene, m_name, m_assetId,
                                                    m_spawnPos, m_spawnRot);
    else
        m_entity = EntityFactory::CreateStaticMesh(scene, m_name, m_assetId,
                                                   m_spawnPos, m_spawnRot);

    if (ctx.registry->valid(m_parent))
        scene.SetParent(m_entity, m_parent);
    scene.MarkMaterialDirty();
    if (ctx.selection) ctx.selection->SelectEntity(m_entity);
}

void CreateStaticMeshCommand::Undo(EditorContext& ctx) {
    if (!ctx.registry->valid(m_entity)) return;
    ctx.scene->DestroyEntity(m_entity);
    m_entity = entt::null;
    if (ctx.selection) ctx.selection->Clear();
}

std::string CreateStaticMeshCommand::GetDescription() const {
    return "Create \"" + m_name + "\"";
}

} // namespace StellarAlia::Editor
