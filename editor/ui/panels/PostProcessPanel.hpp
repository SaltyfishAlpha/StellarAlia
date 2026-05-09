#pragma once

#include "ui/IEditorWindow.hpp"
#include "function/renderer/SceneRenderer.hpp"
#include "function/scene/Scene.hpp"

namespace StellarAlia::Resource { class AssetRegistry; }

namespace StellarAlia::Editor {

class PostProcessPanel : public IEditorWindow {
public:
    PostProcessPanel(Scene& scene, SceneRenderer& renderer,
                     const Resource::AssetRegistry* registry = nullptr)
        : m_scene(&scene), m_renderer(&renderer), m_registry(registry) {}

    std::string_view GetName() const override { return "Post Process"; }
    void OnDraw() override;

private:
    Scene*                         m_scene    = nullptr;
    SceneRenderer*                 m_renderer = nullptr;
    const Resource::AssetRegistry* m_registry = nullptr;
};

} // namespace StellarAlia::Editor
