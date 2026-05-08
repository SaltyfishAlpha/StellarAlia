#pragma once

#include "ui/IEditorWindow.hpp"
#include "ui/IComponentDrawer.hpp"
#include "ui/panels/SceneHierarchyPanel.hpp"

#include <entt/entt.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace StellarAlia         { class Scene; }
namespace StellarAlia::Resource { class AssetRegistry; }

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// InspectorPanel — shows component details for the entity selected in the
// SceneHierarchyPanel.
//
// Component rendering is delegated to IComponentDrawer instances registered in
// the constructor.  Add a new drawer to support a new component type without
// touching OnDraw().
//
// The "Add Component" popup is driven by a registered ComponentDescriptor list.
// Engine components are pre-registered; call RegisterComponent() to add custom
// or game-specific components at startup:
//
//   inspector.RegisterComponent({
//       "Game",
//       "Health",
//       [](auto& reg, auto e) { return reg.any_of<HealthComponent>(e); },
//       [](auto& reg, auto e, auto&) { reg.emplace<HealthComponent>(e); }
//   });
// ─────────────────────────────────────────────────────────────────────────────

struct ComponentDescriptor {
    std::string category;  // section header text (e.g. "Rendering")
    std::string label;     // entry label in the popup
    std::function<bool(entt::registry&, entt::entity)>          hasComp;
    std::function<void(entt::registry&, entt::entity, Scene&)>  addComp;
};

class InspectorPanel : public IEditorWindow {
public:
    // registry may be nullptr; AssetID fields degrade to read-only labels.
    InspectorPanel(Scene& scene,
                   const SceneHierarchyPanel& hierarchy,
                   const Resource::AssetRegistry* registry = nullptr);

    std::string_view GetName() const override { return "Inspector"; }
    void OnDraw() override;

    // Register a component type in the "Add Component" popup.
    // Entries are grouped by category in registration order.
    void RegisterComponent(ComponentDescriptor desc);

private:
    void RegisterDrawers();
    void RegisterBuiltinComponents();

    Scene*                          m_scene     = nullptr;
    const SceneHierarchyPanel*      m_hierarchy = nullptr;
    const Resource::AssetRegistry*  m_registry  = nullptr;

    std::vector<std::unique_ptr<IComponentDrawer>> m_drawers;
    std::vector<ComponentDescriptor>               m_addableComponents;
};

} // namespace StellarAlia::Editor
