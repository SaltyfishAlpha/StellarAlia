#pragma once

#include "command/IEditorCommand.hpp"
#include "function/scene/Components.hpp"
#include "core/asset/AssetID.hpp"

#include <entt/entt.hpp>
#include <glm/vec3.hpp>
#include <optional>
#include <string>

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// DeleteEntityCommand
//
// Snapshot-based. Non-undoable when the entity has children (children would
// become orphaned roots after delete; restoring them correctly is out of scope).
// ─────────────────────────────────────────────────────────────────────────────
class DeleteEntityCommand final : public IEditorCommand {
public:
    explicit DeleteEntityCommand(entt::entity entity);

    void Execute(EditorContext& ctx) override;
    void Undo(EditorContext& ctx) override;
    [[nodiscard]] std::string GetDescription() const override;

private:
    struct Snapshot {
        std::string name;
        entt::entity parent = entt::null;

        std::optional<TransformComponent>         transform;
        std::optional<CameraComponent>            camera;
        std::optional<DirectionalLightComponent>  dirLight;
        std::optional<PointLightComponent>        pointLight;
        std::optional<SpotLightComponent>         spotLight;
        std::optional<AreaLightComponent>         areaLight;
        std::optional<StaticMeshComponent>        staticMesh;
        std::optional<MeshRendererComponent>      meshRenderer;
        std::optional<MaterialOverrideComponent>  matOverride;
        std::optional<RigidBodyComponent>         rigidBody;
        std::optional<ColliderComponent>          collider;
        bool                                      staticGeomTag = false;
    };

    void CaptureSnapshot(const entt::registry& reg);
    void ApplySnapshot(EditorContext& ctx);

    entt::entity m_entity;
    bool         m_undoable  = true;
    bool         m_snapped   = false;   // true once snapshot has been captured
    Snapshot     m_snapshot;
};

// ─────────────────────────────────────────────────────────────────────────────
// RenameEntityCommand
// ─────────────────────────────────────────────────────────────────────────────
class RenameEntityCommand final : public IEditorCommand {
public:
    RenameEntityCommand(entt::entity entity, std::string oldName, std::string newName);

    void Execute(EditorContext& ctx) override;
    void Undo(EditorContext& ctx) override;
    [[nodiscard]] std::string GetDescription() const override;

private:
    entt::entity m_entity;
    std::string  m_oldName;
    std::string  m_newName;
};

// ─────────────────────────────────────────────────────────────────────────────
// ReparentEntityCommand — records a single entity's parent change.
// Undo restores the old parent; sibling order is not preserved.
// ─────────────────────────────────────────────────────────────────────────────
class ReparentEntityCommand final : public IEditorCommand {
public:
    ReparentEntityCommand(entt::entity entity, entt::entity oldParent, entt::entity newParent);

    void Execute(EditorContext& ctx) override;
    void Undo(EditorContext& ctx) override;
    [[nodiscard]] std::string GetDescription() const override;

private:
    entt::entity m_entity;
    entt::entity m_oldParent;
    entt::entity m_newParent;
};

// ─────────────────────────────────────────────────────────────────────────────
// CreateStaticMeshCommand — undoable asset-drop spawn.
// Execute creates the entity; Undo destroys it; Redo re-creates with same params.
// ─────────────────────────────────────────────────────────────────────────────
class CreateStaticMeshCommand final : public IEditorCommand {
public:
    CreateStaticMeshCommand(std::string name, AssetID assetId,
                            entt::entity parent, glm::vec3 spawnPos,
                            glm::quat spawnRot = glm::quat{1.f, 0.f, 0.f, 0.f});

    void Execute(EditorContext& ctx) override;
    void Undo(EditorContext& ctx) override;
    [[nodiscard]] std::string GetDescription() const override;

private:
    std::string  m_name;
    AssetID      m_assetId;
    entt::entity m_parent;
    glm::vec3    m_spawnPos;
    glm::quat    m_spawnRot;
    entt::entity m_entity = entt::null;
};

} // namespace StellarAlia::Editor
