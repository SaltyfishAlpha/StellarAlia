#pragma once

#include "engine/AppMode.hpp"
#include "engine/SaProject.hpp"

#include <optional>
#include "camera/EditorCamera.hpp"
#include "ui/EditorUI.hpp"
#include "resource/EntityTemplateRegistry.hpp"
#include "project/ProjectManager.hpp"
#include "ui/panels/ProjectBrowserPanel.hpp"
#include "EditorDiagnostics.hpp"
#include "EditorLogCapture.hpp"
#include "config/EditorShortcutConfig.hpp"
#include "EditorContext.hpp"
#include "EditorSelection.hpp"
#include "ui/drawers/ComponentDrawerRegistry.hpp"
#include "ui/presenters/SceneHierarchyPresenter.hpp"
#include "ui/presenters/AssetsPresenter.hpp"
#include "ui/presenters/PlaybackPresenter.hpp"
#include "ui/presenters/WorldSettingsPresenter.hpp"
#include "ui/presenters/PostProcessPresenter.hpp"
#include "ui/presenters/ShortcutsPresenter.hpp"
#include "ui/presenters/ProjectBrowserPresenter.hpp"
#include "ui/presenters/ConsolePanelPresenter.hpp"
#include "action/EditorActionRegistry.hpp"
#include "command/CommandManager.hpp"
#include "function/scene/Components.hpp"

#include "core/spatial/BVHTree.hpp"
#include "ui/EditorIconCache.hpp"
#include "platform/io/FileWatcher.hpp"

#include <entt/entt.hpp>
#include <imgui.h>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
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
    bool                       m_gameMapPushed       = false;  // PIE-side: project .sainputmap active
    bool                       m_editorGlobalPushed  = false;  // Phase 3b: kept across PIE

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
    EditorSelection                        m_selection;
    ComponentDrawerRegistry                m_drawerRegistry;
    std::unique_ptr<SceneHierarchyPresenter>  m_hierPresenter;
    std::unique_ptr<AssetsPresenter>          m_assetsPresenter;
    std::unique_ptr<PlaybackPresenter>        m_playbackPresenter;
    std::unique_ptr<WorldSettingsPresenter>   m_worldPresenter;
    std::unique_ptr<PostProcessPresenter>     m_ppPresenter;
    std::unique_ptr<ShortcutsPresenter>       m_shortcutsPresenter;
    std::unique_ptr<ProjectBrowserPresenter>  m_projectBrowserPresenter;
    std::unique_ptr<ConsolePanelPresenter>    m_consolePresenter;
    EditorActionRegistry                      m_actionRegistry;
    CommandManager                            m_commandManager;
    EditorContext                             m_ctx;

    Platform::FileWatcher      m_scriptWatcher;
    bool                       m_pendingRecompile  = false;
    bool                       m_pendingShaderCook = false;  // Issue #90: focus-triggered .saglsl/.saeffect cook

    std::filesystem::path      m_currentScenePath;
    std::filesystem::path      m_pendingProjectLoad;   // deferred — set in OnRenderUI, executed in OnUpdate
    bool                       m_pendingSaveAs   = false; // deferred — set in render phase, NFD runs in OnUpdate
    bool                       m_gizmoIsUsing    = false;
    TransformComponent         m_gizmoDragStart  = {};

    void BuildContext(Application& app);
    void DrawOverlays();
    void DrawBillboardIcons();
    void DrawImGuizmo();
    void HandleViewportInteraction();
    [[nodiscard]] Core::Ray ScreenToWorldRay(float sx, float sy) const;
    static bool RayHitHorizontalPlane(const Core::Ray& ray, float planeY, glm::vec3& outHit);
    void LoadScene(const std::filesystem::path& path);
    void LoadProject(const std::filesystem::path& saprojectPath);
    // Shared by OnAttach (initial load) and LoadProject (project switch). Touches
    // only the filesystem-level wiring: script watcher, asset registry, user-
    // script compile, AssetsPanel root, and startup-scene load. Callers handle
    // their own ordering of clear/cook/apply steps around this. Returns the
    // parsed .saproject when one was found (nullopt = no .saproject in dir).
    [[nodiscard]] std::optional<SaProject> LoadProjectFiles(const std::filesystem::path& projectDir);
    void NewScene();
    void SaveScene();
    void CookProjectShaders();

    // ── #106: @ShadingModel rename detection → .samat migration prompt ────────
    // CookDirectory writes generated/shaders/shader_models.txt (source → model);
    // cooks snapshot it before and diff after — same source, different model =
    // rename that orphans .samat "type" references.
    struct ShaderTypeRename {
        std::string source;               // .saglsl generic path
        std::string oldName, newName;
        int         samatCount = 0;       // referencing .samat found at detection
    };
    std::vector<ShaderTypeRename> m_pendingTypeRenames;

    // assetsDir passed explicitly: LoadProject cooks BEFORE UpdateProjectPaths,
    // so GetDesc().projectDir would still point at the previous project there.
    void DetectShaderTypeRenames(
        const std::unordered_map<std::string, std::string>& before,
        const std::filesystem::path& dispatchDir,
        const std::filesystem::path& assetsDir);
    void DrawShaderRenameModal();
    void MigrateSamatTypes(const std::string& oldName, const std::string& newName);
};

} // namespace StellarAlia::Editor
