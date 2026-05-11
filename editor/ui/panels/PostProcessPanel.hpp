#pragma once

#include "ui/IEditorWindow.hpp"
#include "ui/presenters/PostProcessPresenter.hpp"
#include "EditorContext.hpp"
#include "function/scene/Scene.hpp"

namespace StellarAlia::Resource { class AssetRegistry; }

namespace StellarAlia::Editor {

class PostProcessPanel : public IEditorWindow {
public:
    PostProcessPanel(EditorContext& ctx, PostProcessPresenter& presenter);

    std::string_view GetName() const override { return "Post Process"; }
    void OnDraw() override;

private:
    PostProcessPresenter&          m_presenter;
    Scene*                         m_scene    = nullptr;
    const Resource::AssetRegistry* m_registry = nullptr;
};

} // namespace StellarAlia::Editor
