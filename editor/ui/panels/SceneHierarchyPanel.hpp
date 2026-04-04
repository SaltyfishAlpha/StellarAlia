#pragma once

#include <cstdint>

#include "ui/IEditorWindow.hpp"

namespace StellarAlia { class Scene; }

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// SceneHierarchyPanel — lists every entity in the scene by name.
// Clicking an entity selects it; the InspectorPanel reads the selection.
// ─────────────────────────────────────────────────────────────────────────────
class SceneHierarchyPanel : public IEditorWindow {
public:
    explicit SceneHierarchyPanel(Scene& scene) : m_scene(&scene) {}

    std::string_view GetName() const override { return "Scene Hierarchy"; }
    void OnDraw() override;

    // Returns the currently selected entity (entt::null if none).
    // Cast to entt::entity at the call site.
    uint32_t GetSelectedEntity() const { return m_selected; }

private:
    Scene*   m_scene    = nullptr;
    uint32_t m_selected = ~0u;   // entt::null equivalent
};

} // namespace StellarAlia::Editor
