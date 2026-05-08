#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>

#include "ui/IEditorWindow.hpp"
#include <entt/entt.hpp>

namespace StellarAlia { class Scene; }
namespace StellarAlia { class InputSystem; }
namespace StellarAlia::Resource { class AssetRegistry; }
namespace StellarAlia::Editor { class EntityTemplateRegistry; }

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// SceneHierarchyPanel — lists every entity in the scene by name.
// Clicking an entity selects it; the InspectorPanel reads the selection.
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
    using SceneLoadCallback = std::function<void(const std::filesystem::path&)>;

    SceneHierarchyPanel(Scene& scene, InputSystem& input)
        : m_scene(&scene), m_input(&input) {}

    // Optional: provide asset registry for asset-drop mesh instantiation.
    void SetRegistry(const Resource::AssetRegistry* registry);

    // Optional: provide template registry to drive the spawn menu.
    void SetTemplateRegistry(const EntityTemplateRegistry* tmplRegistry);

    // Optional: called when a .sascene is dropped onto the panel.
    void SetSceneLoadCallback(SceneLoadCallback cb);

    std::string_view GetName()    const override { return "Scene Hierarchy"; }
    ImGuiWindowFlags GetWindowFlags() const override { return ImGuiWindowFlags_HorizontalScrollbar; }
    void OnDraw() override;

    // Returns the currently selected entity raw bits (~0u = none).
    uint32_t GetSelectedEntity() const { return m_selected; }

    // Called from the top menu bar Entity entries to create at scene root.
    void RequestCreateEmpty();
    void RequestSpawnTemplate(const std::filesystem::path& templatePath);

private:
    void DrawNode(entt::entity entity, entt::registry& reg);

    // Copies all value-type components from src to a new entity.
    // Children are recursively duplicated and re-parented.
    entt::entity DuplicateEntity(entt::entity src);

    Scene*                           m_scene          = nullptr;
    InputSystem*                     m_input          = nullptr;
    const Resource::AssetRegistry*   m_registry       = nullptr;
    const EntityTemplateRegistry*    m_tmplRegistry   = nullptr;
    SceneLoadCallback                m_onSceneLoad;
    uint32_t                         m_selected = ~0u;


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
    entt::entity m_pendingDelete    = entt::null;
    entt::entity m_pendingDuplicate = entt::null;

    // ── Drag-and-drop ──────────────────────────────────────────────────────
    struct DnDOp {
        entt::entity dragged = entt::null;
        entt::entity target  = entt::null;  // entt::null = detach to root
        enum Mode : uint8_t { AsChild, BeforeSibling, AfterSibling } mode = AsChild;
        bool valid = false;
    };
    DnDOp m_pendingDnD;

    struct AssetDropOp {
        std::filesystem::path assetPath;
        entt::entity          parent = entt::null;  // entt::null = create at root
        bool                  valid  = false;
    };
    AssetDropOp m_pendingAssetDrop;
};

} // namespace StellarAlia::Editor
