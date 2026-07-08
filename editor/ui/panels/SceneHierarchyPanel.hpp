#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <unordered_set>
#include <vector>

#include "ui/IEditorWindow.hpp"
#include "ui/DoubleClickClassifier.hpp"
#include "EditorContext.hpp"
#include "ui/presenters/SceneHierarchyPresenter.hpp"
#include <entt/entt.hpp>
#include <glm/vec3.hpp>

namespace StellarAlia { class Scene; }
namespace StellarAlia { class InputSystem; }
namespace StellarAlia::Resource { class AssetRegistry; }
namespace StellarAlia::Editor { class EntityTemplateRegistry; }
namespace StellarAlia::Editor { class CommandManager; }

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// SceneHierarchyPanel — lists every entity in the scene by name.
// Clicking an entity selects it; the InspectorPanel reads the selection.
//
// Selection model:
//   Normal click   — single-select (clears previous selection)
//   Ctrl+click     — toggle individual entity
//   Shift+click    — range-select from last anchor to clicked entity
//   Ctrl+A         — select all entities
//
// Supports entity CRUD via toolbar button, right-click context menu, and
// keyboard shortcuts driven by the InputSystem.
//
// Drag-and-drop:
//   Drag any node, then drop onto another to reparent or reorder:
//     Top 30%   of target → insert before as sibling
//     Middle 40% of target → make child of target
//     Bottom 30% of target → insert after as sibling
//   Drop onto empty window space → detach to scene root.
//
//   Assets dragged from the AssetsPanel (payload "SAASSET") are dispatched by
//   extension: .glb/.gltf → instantiate StaticMesh; .sascene → open scene;
//   other types (textures, materials, animations) → no-op.
// ─────────────────────────────────────────────────────────────────────────────
class SceneHierarchyPanel : public IEditorWindow {
public:
    SceneHierarchyPanel(EditorContext& ctx, SceneHierarchyPresenter& presenter)
        : m_scene(ctx.scene)
        , m_input(ctx.input)
        , m_registry(ctx.assetReg)
        , m_tmplRegistry(ctx.templateReg)
        , m_selectionCtx(ctx.selection)
        , m_cmdMgr(ctx.cmdMgr)
        , m_onSceneLoad(ctx.onSceneLoad)
        , m_onFocusEntity(ctx.onFocusEntity)
        , m_presenter(presenter) {}

    std::string_view GetName()    const override { return "Scene Hierarchy"; }
    ImGuiWindowFlags GetWindowFlags() const override { return ImGuiWindowFlags_HorizontalScrollbar; }
    void OnDraw() override;

    // Called from the top menu bar Entity entries to create at scene root.
    void RequestCreateEmpty();
    void RequestSpawnTemplate(const std::filesystem::path& templatePath);

    // Set single-entity selection (from viewport picking).
    void SetSelection(entt::entity e);
    // Clear the selection entirely.
    void ClearSelection();
    // Trigger an asset drop spawn at a specific world position (from viewport drop).
    // spawnRot: surface-normal alignment when enabled (Issue #111).
    void TriggerAssetDrop(const std::filesystem::path& assetPath, const glm::vec3& spawnPos,
                          const glm::quat& spawnRot = {1.f, 0.f, 0.f, 0.f});

private:
    void DrawNode(entt::entity entity, entt::registry& reg);

    // Select a contiguous range of entities from m_shiftAnchor to 'to'
    // using the previous frame's visual draw order.
    void SelectRange(uint32_t to);

    // Push current m_selection / m_primarySelected state to EditorSelection.
    void SyncSelectionToCtx();

    Scene*                           m_scene          = nullptr;
    InputSystem*                     m_input          = nullptr;
    const Resource::AssetRegistry*   m_registry       = nullptr;
    const EntityTemplateRegistry*    m_tmplRegistry   = nullptr;
    EditorSelection*                 m_selectionCtx   = nullptr;
    CommandManager*                  m_cmdMgr         = nullptr;
    std::function<void(const std::filesystem::path&)> m_onSceneLoad;
    std::function<void(glm::vec3)>                    m_onFocusEntity;
    SceneHierarchyPresenter&         m_presenter;

    // ── Selection state ────────────────────────────────────────────────────
    std::unordered_set<uint32_t>  m_selection;                // all selected entities
    uint32_t                      m_primarySelected = ~0u;    // inspector target + rename anchor
    uint32_t                      m_shiftAnchor     = ~0u;    // Shift+click range start

    // Visual draw order from the previous frame — used for Shift+click range selection.
    std::vector<uint32_t>         m_drawOrder;
    std::vector<uint32_t>         m_drawOrderBuild;           // accumulated this frame

    // ── Double-click classification ────────────────────────────────────────
    DoubleClickClassifier            m_dblClick;
    uint32_t                         m_dblClickEntity = ~0u;

    // ── Pending deselect (deferred single-select so drag can start first) ────
    uint32_t m_pendingDeselectOthers = ~0u;

    // ── Rename state ───────────────────────────────────────────────────────
    uint32_t m_renamingEntity  = ~0u;
    char     m_renameBuffer[256] = {};
    bool     m_renameFocusNext = false;
};

} // namespace StellarAlia::Editor
