#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <unordered_set>
#include <vector>

#include "ui/IEditorWindow.hpp"
#include "ui/DoubleClickClassifier.hpp"
#include <entt/entt.hpp>
#include <glm/vec3.hpp>

namespace StellarAlia { class Scene; }
namespace StellarAlia { class InputSystem; }
namespace StellarAlia::Resource { class AssetRegistry; }
namespace StellarAlia::Editor { class EntityTemplateRegistry; }

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
    using SceneLoadCallback     = std::function<void(const std::filesystem::path&)>;
    using FocusEntityCallback   = std::function<void(glm::vec3)>;

    SceneHierarchyPanel(Scene& scene, InputSystem& input)
        : m_scene(&scene), m_input(&input) {}

    // Optional: provide asset registry for asset-drop mesh instantiation.
    void SetRegistry(const Resource::AssetRegistry* registry);

    // Optional: provide template registry to drive the spawn menu.
    void SetTemplateRegistry(const EntityTemplateRegistry* tmplRegistry);

    // Optional: called when a .sascene is dropped onto the panel.
    void SetSceneLoadCallback(SceneLoadCallback cb);

    // Optional: called on short double-click with the entity's world position.
    void SetFocusEntityCallback(FocusEntityCallback cb);

    std::string_view GetName()    const override { return "Scene Hierarchy"; }
    ImGuiWindowFlags GetWindowFlags() const override { return ImGuiWindowFlags_HorizontalScrollbar; }
    void OnDraw() override;

    // Primary selected entity raw bits (~0u = none).
    uint32_t GetSelectedEntity() const { return m_primarySelected; }

    // All currently selected entity raw bits.
    const std::unordered_set<uint32_t>& GetSelectedEntities() const { return m_selection; }

    // Called from the top menu bar Entity entries to create at scene root.
    void RequestCreateEmpty();
    void RequestSpawnTemplate(const std::filesystem::path& templatePath);

    // Set single-entity selection (from viewport picking).
    void SetSelection(entt::entity e);
    // Clear the selection entirely.
    void ClearSelection();
    // Trigger an asset drop spawn at a specific world position (from viewport drop).
    void TriggerAssetDrop(const std::filesystem::path& assetPath, const glm::vec3& spawnPos);

private:
    void DrawNode(entt::entity entity, entt::registry& reg);

    // Select a contiguous range of entities from m_shiftAnchor to 'to'
    // using the previous frame's visual draw order.
    void SelectRange(uint32_t to);

    // Copies all value-type components from src to a new entity.
    // Children are recursively duplicated and re-parented.
    entt::entity DuplicateEntity(entt::entity src);

    Scene*                           m_scene          = nullptr;
    InputSystem*                     m_input          = nullptr;
    const Resource::AssetRegistry*   m_registry       = nullptr;
    const EntityTemplateRegistry*    m_tmplRegistry   = nullptr;
    SceneLoadCallback                m_onSceneLoad;
    FocusEntityCallback              m_onFocusEntity;

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
    bool     m_renameFocusNext = false; // focus the InputText on the next frame it appears

    // ── Deferred create operation ──────────────────────────────────────────
    struct CreateOp {
        enum Kind : uint8_t { None, Empty, Template } kind = None;
        std::filesystem::path templatePath;   // for Template kind
        entt::entity          parent = entt::null;  // entt::null = scene root
    };
    CreateOp m_pendingCreate;

    // ── Deferred operations (applied after tree traversal) ─────────────────
    std::vector<entt::entity> m_pendingDeletes;
    std::vector<entt::entity> m_pendingDuplicates;

    // ── Drag-and-drop ──────────────────────────────────────────────────────
    struct DnDOp {
        std::vector<entt::entity> dragged;             // one or many (multi-select drag)
        entt::entity              target = entt::null; // entt::null = detach to root
        enum Mode : uint8_t { AsChild, BeforeSibling, AfterSibling } mode = AsChild;
        bool valid = false;
    };
    DnDOp m_pendingDnD;

    struct AssetDropOp {
        std::filesystem::path assetPath;
        entt::entity          parent   = entt::null;  // entt::null = create at root
        glm::vec3             spawnPos = {};
        bool                  valid    = false;
    };
    AssetDropOp m_pendingAssetDrop;
};

} // namespace StellarAlia::Editor
