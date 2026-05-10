#pragma once

#include "ui/IEditorWindow.hpp"
#include "ui/IComponentDrawer.hpp"
#include "ui/IAssetInspector.hpp"
#include "ui/panels/SceneHierarchyPanel.hpp"

#include <imgui.h>
#include <entt/entt.hpp>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace StellarAlia           { class Scene; class MaterialManager; }
namespace StellarAlia::Resource { class AssetRegistry; }

namespace StellarAlia::Editor {

class AssetsPanel;
class EditorIconCache;

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
    // registry / matMgr may be nullptr; asset fields degrade to read-only labels.
    InspectorPanel(Scene& scene,
                   const SceneHierarchyPanel& hierarchy,
                   const Resource::AssetRegistry* registry = nullptr,
                   const MaterialManager*          matMgr   = nullptr);

    std::string_view GetName() const override { return "Inspector"; }
    void OnDraw() override;

    // Wire the Assets panel so the Inspector can show asset details on selection.
    void SetAssetsPanel(const AssetsPanel* panel) { m_assetsPanel = panel; }

    // Wire the icon cache for image thumbnail display.
    void SetIconCache(EditorIconCache* cache);

    // Wire the FA6 icon font for component drawers that render inline icons.
    void SetIconFont(ImFont* font);


    // Register a component type in the "Add Component" popup.
    // Entries are grouped by category in registration order.
    void RegisterComponent(ComponentDescriptor desc);

private:
    enum class Mode { Entity, Asset };

    void RegisterDrawers();
    void RegisterBuiltinComponents();
    void RegisterAssetDrawers();
    void DrawEntityInspector(uint32_t sel);
    void DrawAssetInspector(const std::filesystem::path& path);

    Scene*                          m_scene      = nullptr;
    const SceneHierarchyPanel*      m_hierarchy  = nullptr;
    const Resource::AssetRegistry*  m_registry   = nullptr;
    const MaterialManager*          m_matMgr     = nullptr;
    const AssetsPanel*              m_assetsPanel = nullptr;
    EditorIconCache*                m_iconCache   = nullptr;
    ImFont*                         m_iconFont    = nullptr;

    std::vector<std::unique_ptr<IComponentDrawer>> m_drawers;
    std::vector<ComponentDescriptor>               m_addableComponents;

    std::unordered_map<std::string, std::unique_ptr<IAssetInspector>> m_assetDrawers;
    std::unique_ptr<IAssetInspector>                                   m_defaultAssetDrawer;

    Mode                  m_mode       = Mode::Entity;
    uint32_t              m_lastEntity = ~0u;
    std::filesystem::path m_lastAsset;
};

} // namespace StellarAlia::Editor
