#pragma once

#include "ui/IEditorWindow.hpp"
#include "ui/IComponentDrawer.hpp"
#include "ui/panels/SceneHierarchyPanel.hpp"

#include <memory>
#include <vector>

namespace StellarAlia { class Scene; }

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// InspectorPanel — shows component details for the entity selected in the
// SceneHierarchyPanel.
//
// Component rendering is delegated to IComponentDrawer instances registered in
// the constructor.  Add a new drawer to support a new component type without
// touching OnDraw().
// ─────────────────────────────────────────────────────────────────────────────
class InspectorPanel : public IEditorWindow {
public:
    InspectorPanel(Scene& scene, const SceneHierarchyPanel& hierarchy);

    std::string_view GetName() const override { return "Inspector"; }
    void OnDraw() override;

private:
    void RegisterDrawers();

    Scene*                      m_scene     = nullptr;
    const SceneHierarchyPanel*  m_hierarchy = nullptr;

    std::vector<std::unique_ptr<IComponentDrawer>> m_drawers;
};

} // namespace StellarAlia::Editor
