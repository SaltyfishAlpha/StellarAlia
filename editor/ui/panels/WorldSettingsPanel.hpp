#pragma once

#include "ui/IEditorWindow.hpp"
#include "function/renderer/SceneRenderer.hpp"
#include "function/scene/Scene.hpp"

namespace StellarAlia::Resource { class AssetRegistry; }

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// WorldSettingsPanel — Background, IBL bake, and Tonemap configuration.
//
// Edits Scene::WorldSettings directly. "Apply Settings" pushes the current
// WorldSettings into SceneRenderer (triggering IBL load and tonemap switching).
// ─────────────────────────────────────────────────────────────────────────────
class WorldSettingsPanel : public IEditorWindow {
public:
    WorldSettingsPanel(Scene& scene, SceneRenderer& renderer,
                       const Resource::AssetRegistry* registry = nullptr)
        : m_scene(&scene), m_renderer(&renderer), m_registry(registry) {}

    std::string_view GetName() const override { return "World Settings"; }
    void OnDraw() override;

private:
    Scene*                         m_scene    = nullptr;
    SceneRenderer*                 m_renderer = nullptr;
    const Resource::AssetRegistry* m_registry = nullptr;
};

} // namespace StellarAlia::Editor
