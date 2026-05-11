#pragma once

#include "ui/presenters/IPresenter.hpp"
#include "EditorContext.hpp"

#include <entt/entt.hpp>
#include <filesystem>
#include <glm/vec3.hpp>
#include <vector>

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// SceneHierarchyPresenter — owns all deferred scene-write operations for
// SceneHierarchyPanel.  Update() executes them once per frame from
// EditorMode::OnUpdate, keeping scene mutations out of OnDraw.
// ─────────────────────────────────────────────────────────────────────────────
class SceneHierarchyPresenter final : public IPresenter {
public:
    explicit SceneHierarchyPresenter(EditorContext& ctx);
    void Update(float dt) override;

    // ── Op type definitions (shared with the panel view) ───────────────────
    struct CreateOp {
        enum Kind : uint8_t { None, Empty, Template } kind = None;
        std::filesystem::path templatePath;
        entt::entity          parent = entt::null;
    };

    enum class DnDMode : uint8_t { AsChild, BeforeSibling, AfterSibling };

    struct AssetDropOp {
        std::filesystem::path assetPath;
        entt::entity          parent   = entt::null;
        glm::vec3             spawnPos = {};
        bool                  valid    = false;
    };

    // ── Request interface (called from SceneHierarchyPanel::OnDraw) ─────────
    void RequestCreate(CreateOp::Kind kind, const std::filesystem::path& templatePath,
                       entt::entity parent);
    void RequestDelete(std::vector<entt::entity> entities);
    void RequestDuplicate(std::vector<entt::entity> entities);
    // 'ordered' entities must already be in visual draw order (caller responsibility).
    void RequestReparent(std::vector<entt::entity> ordered, entt::entity target, DnDMode mode);
    void RequestAssetDrop(AssetDropOp op);

private:
    entt::entity DuplicateEntity(entt::entity src);

    EditorContext& m_ctx;

    CreateOp                   m_pendingCreate;
    std::vector<entt::entity>  m_pendingDeletes;
    std::vector<entt::entity>  m_pendingDuplicates;

    struct DnDOp {
        std::vector<entt::entity> ordered;
        entt::entity              target = entt::null;
        DnDMode                   mode   = DnDMode::AsChild;
        bool                      valid  = false;
    };
    DnDOp        m_pendingDnD;
    AssetDropOp  m_pendingAssetDrop;
};

} // namespace StellarAlia::Editor
