#pragma once

#include "ui/IEditorWindow.hpp"
#include "ui/presenters/WorldSettingsPresenter.hpp"
#include "EditorContext.hpp"
#include "function/scene/Scene.hpp"

namespace StellarAlia::Resource { class AssetRegistry; }

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// WorldSettingsPanel — Background, IBL bake, and Tonemap configuration.
//
// Edits Scene::WorldSettings directly. Apply/Bake calls are deferred to
// WorldSettingsPresenter so SceneRenderer is not touched from OnDraw.
// ─────────────────────────────────────────────────────────────────────────────
class WorldSettingsPanel : public IEditorWindow {
public:
    WorldSettingsPanel(EditorContext& ctx, WorldSettingsPresenter& presenter);

    std::string_view GetName() const override { return "World Settings"; }
    void OnDraw() override;

private:
    WorldSettingsPresenter&        m_presenter;
    Scene*                         m_scene    = nullptr;
    const Resource::AssetRegistry* m_registry = nullptr;
};

} // namespace StellarAlia::Editor
