#pragma once

#include "ui/IEditorWindow.hpp"
#include "ui/panels/SceneHierarchyPanel.hpp"

namespace StellarAlia { class Scene; }

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// InspectorPanel — shows component details for the entity selected in the
// SceneHierarchyPanel.
// ─────────────────────────────────────────────────────────────────────────────
class InspectorPanel : public IEditorWindow {
public:
    InspectorPanel(Scene& scene, const SceneHierarchyPanel& hierarchy)
        : m_scene(&scene), m_hierarchy(&hierarchy) {}

    std::string_view GetName() const override { return "Inspector"; }
    void OnDraw() override;

private:
    Scene*                      m_scene     = nullptr;
    const SceneHierarchyPanel*  m_hierarchy = nullptr;
};

} // namespace StellarAlia::Editor
