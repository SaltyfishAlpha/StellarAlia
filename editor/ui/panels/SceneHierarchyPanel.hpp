#pragma once

#include <cstdint>
#include <cstring>

#include "ui/IEditorWindow.hpp"
#include <entt/entt.hpp>

namespace StellarAlia { class Scene; }
namespace StellarAlia { class InputSystem; }

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
// ─────────────────────────────────────────────────────────────────────────────
class SceneHierarchyPanel : public IEditorWindow {
public:
    SceneHierarchyPanel(Scene& scene, InputSystem& input)
        : m_scene(&scene), m_input(&input) {}

    std::string_view GetName() const override { return "Scene Hierarchy"; }
    void OnDraw() override;

    // Returns the currently selected entity raw bits (~0u = none).
    uint32_t GetSelectedEntity() const { return m_selected; }

private:
    void DrawNode(entt::entity entity, entt::registry& reg);

    // Copies all value-type components from src to a new entity.
    // Children are recursively duplicated and re-parented.
    entt::entity DuplicateEntity(entt::entity src);

    Scene*       m_scene = nullptr;
    InputSystem* m_input = nullptr;
    uint32_t     m_selected = ~0u;

    // ── Rename state ───────────────────────────────────────────────────────
    uint32_t m_renamingEntity  = ~0u;
    char     m_renameBuffer[256] = {};
    bool     m_renameOpenPopup = false;

    // ── Entity create kind (shared between root and child pending ops) ─────
    enum class CreateKind : uint8_t { Empty, Cube, Plane };
    CreateKind m_createKind = CreateKind::Empty;

    // ── Deferred operations (applied after tree traversal) ─────────────────
    entt::entity m_pendingDelete      = entt::null;
    entt::entity m_pendingDuplicate   = entt::null;
    entt::entity m_pendingCreateChild = entt::null;
    bool         m_pendingCreateRoot  = false;

    // ── Drag-and-drop ──────────────────────────────────────────────────────
    struct DnDOp {
        entt::entity dragged = entt::null;
        entt::entity target  = entt::null;  // entt::null = detach to root
        enum Mode : uint8_t { AsChild, BeforeSibling, AfterSibling } mode = AsChild;
        bool valid = false;
    };
    DnDOp m_pendingDnD;
};

} // namespace StellarAlia::Editor
