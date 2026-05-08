#pragma once

#include "engine/AppMode.hpp"
#include "camera/EditorCamera.hpp"
#include "ui/EditorUI.hpp"

#include <filesystem>
#include "EditorOverlaySettings.hpp"
#include "gizmo/GizmoSystem.hpp"

namespace StellarAlia             { class Application; }
namespace StellarAlia::Resource   { class AssetRegistry; }

namespace StellarAlia::Editor {

class SceneHierarchyPanel;
class AssetsPanel;

// ─────────────────────────────────────────────────────────────────────────────
// EditorMode — the engine's authoring mode.
//
// Owns the editor viewport camera. Registers the Viewport / UI action maps
// and drives the camera from input each frame.
//
// Lifetime: created by main(), handed to Application, alive until Shutdown().
// ─────────────────────────────────────────────────────────────────────────────
class EditorMode final : public AppMode {
public:
    void OnAttach(Application& app) override;
    void OnDetach() override;
    void OnUpdate(float dt) override;
    void OnRenderUI(RHI::IRHICommandList* cmd) override;
    [[nodiscard]] CameraData GetCameraData(float aspectRatio) const override;
    void OnPlayStateChanged(EnginePlayState newState) override;

private:
    Application*               m_app             = nullptr;
    EditorCamera               m_camera;
    EditorUI                   m_ui;
    bool                       m_viewportActive  = false;

    EditorOverlaySettings       m_overlaySettings;
    const SceneHierarchyPanel*  m_hierarchyPanel  = nullptr;
    AssetsPanel*                m_assetsPanel     = nullptr;
    Resource::AssetRegistry*    m_assetRegistry   = nullptr;
    GizmoSystem                 m_gizmo;

    void DrawOverlays();
    void LoadScene(const std::filesystem::path& path);
};

} // namespace StellarAlia::Editor
