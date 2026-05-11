#pragma once

#include "ui/IEditorWindow.hpp"
#include "ui/IAssetInspector.hpp"
#include "EditorContext.hpp"
#include "EditorSelection.hpp"

#include <imgui.h>
#include <entt/entt.hpp>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace StellarAlia { class Scene; }

namespace StellarAlia::Editor {

class EditorIconCache;

// ─────────────────────────────────────────────────────────────────────────────
// InspectorPanel — shows component details for the entity or asset currently
// selected via EditorSelection.
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
    explicit InspectorPanel(EditorContext& ctx);

    std::string_view GetName() const override { return "Inspector"; }
    void OnDraw() override;


    // Register a component type in the "Add Component" popup.
    // Entries are grouped by category in registration order.
    void RegisterComponent(ComponentDescriptor desc);

private:
    void RegisterBuiltinComponents();
    void RegisterAssetDrawers();
    void DrawEntityInspector(uint32_t sel);
    void DrawAssetInspector(const std::filesystem::path& path);

    EditorContext*             m_ctx        = nullptr;
    Scene*                     m_scene      = nullptr;
    const EditorSelection*     m_selection  = nullptr;
    EditorIconCache*           m_iconCache  = nullptr;

    std::vector<ComponentDescriptor>               m_addableComponents;

    std::unordered_map<std::string, std::unique_ptr<IAssetInspector>> m_assetDrawers;
    std::unique_ptr<IAssetInspector>                                   m_defaultAssetDrawer;
};

} // namespace StellarAlia::Editor
