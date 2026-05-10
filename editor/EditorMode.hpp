#pragma once

#include "engine/AppMode.hpp"
#include "camera/EditorCamera.hpp"
#include "ui/EditorUI.hpp"
#include "resource/EntityTemplateRegistry.hpp"
#include "project/ProjectManager.hpp"
#include "ui/panels/ProjectBrowserPanel.hpp"
#include "EditorDiagnostics.hpp"
#include "EditorLogCapture.hpp"
#include "config/EditorShortcutConfig.hpp"

#include "core/spatial/BVHTree.hpp"
#include "ui/EditorIconCache.hpp"

#include <entt/entt.hpp>
#include <imgui.h>
#include <filesystem>
#include <memory>
#include <vector>
#include "EditorOverlaySettings.hpp"

namespace StellarAlia             { class Application; }
namespace StellarAlia::Resource   { class AssetRegistry; }

namespace StellarAlia::Editor {

class SceneHierarchyPanel;
class AssetsPanel;
class InspectorPanel;

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
    bool                       m_viewportActive      = false;
    bool                       m_textInputMapPushed  = false;

    EditorDiagnostics                 m_diagnostics;
    std::unique_ptr<EditorLogCapture> m_logCapture;

    EditorOverlaySettings                       m_overlaySettings;
    std::unique_ptr<EditorIconCache>            m_iconCache;

    struct BillboardHit { entt::entity entity; ImVec2 screenPos; };
    std::vector<BillboardHit>                   m_billboardHits;

    SceneHierarchyPanel*       m_hierarchyPanel  = nullptr;
    AssetsPanel*               m_assetsPanel     = nullptr;
    InspectorPanel*            m_inspectorPanel  = nullptr;
    Resource::AssetRegistry*   m_assetRegistry   = nullptr;

    EntityTemplateRegistry                 m_templateRegistry;
    ProjectManager                         m_projectManager;
    std::unique_ptr<ProjectBrowserPanel>   m_projectBrowserPanel;
    std::filesystem::path                  m_recentsConfigPath;
    bool                                   m_showProjectBrowser = false;

    EditorShortcutConfig                   m_shortcutConfig;

    std::filesystem::path      m_currentScenePath;
    std::filesystem::path      m_pendingProjectLoad;   // deferred — set in OnRenderUI, executed in OnUpdate
    bool                       m_gizmoIsUsing    = false;

    void DrawOverlays();
    void DrawBillboardIcons();
    void DrawImGuizmo();
    void HandleViewportInteraction();
    [[nodiscard]] Core::Ray ScreenToWorldRay(float sx, float sy) const;
    static bool RayHitHorizontalPlane(const Core::Ray& ray, float planeY, glm::vec3& outHit);
    void LoadScene(const std::filesystem::path& path);
    void LoadProject(const std::filesystem::path& saprojectPath);
    void NewScene();
    void SaveScene();
    void CookProjectShaders();
};

} // namespace StellarAlia::Editor
