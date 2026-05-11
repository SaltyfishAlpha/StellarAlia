#pragma once

#include <entt/entt.hpp>
#include <functional>
#include <filesystem>
#include <glm/vec3.hpp>

struct ImFont;

namespace StellarAlia {
    class Application;
    class Scene;
    class MaterialManager;
    class InputSystem;
}
namespace StellarAlia::Resource {
    class AssetRegistry;
    class ResourceManager;
}
namespace StellarAlia::Editor {
    class EditorSelection;
    class EditorDiagnostics;
    class EditorLogCapture;
    class EditorIconCache;
    class EditorShortcutConfig;
    class EditorOverlaySettings;
    class EntityTemplateRegistry;
    class ProjectManager;
    class ComponentDrawerRegistry;
    class EditorActionRegistry;
    class CommandManager;
}

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// EditorContext — single dependency-injection container passed to every panel.
//
// All pointer fields are non-owning; lifetimes are guaranteed by EditorMode.
// EditorMode::BuildContext() fills this struct before constructing any panel.
// ─────────────────────────────────────────────────────────────────────────────
struct EditorContext {
    // ── Engine systems (owned by Application) ─────────────────────────────────
    Application*             app         = nullptr;
    Scene*                   scene       = nullptr;
    entt::registry*          registry    = nullptr;
    Resource::AssetRegistry* assetReg    = nullptr;
    MaterialManager*         matMgr      = nullptr;
    Resource::ResourceManager* resMgr    = nullptr;
    InputSystem*             input       = nullptr;

    // ── Editor systems (owned by EditorMode) ──────────────────────────────────
    EditorSelection*         selection       = nullptr;   // Issue #63
    EditorDiagnostics*       diagnostics     = nullptr;
    EditorLogCapture*        logCapture      = nullptr;
    EditorIconCache*         iconCache       = nullptr;
    ImFont*                  iconFont        = nullptr;
    EditorShortcutConfig*    shortcuts       = nullptr;
    EditorOverlaySettings*   overlaySettings = nullptr;
    EntityTemplateRegistry*  templateReg     = nullptr;
    ProjectManager*          projectMgr      = nullptr;
    ComponentDrawerRegistry* drawerRegistry  = nullptr;
    EditorActionRegistry*    actionReg       = nullptr;
    CommandManager*          cmdMgr          = nullptr;

    // ── Project context ────────────────────────────────────────────────────────
    std::filesystem::path    projectDir;

    // ── Callbacks (lambdas set by EditorMode::BuildContext) ───────────────────
    std::function<void(const std::filesystem::path&)>   onSceneLoad;
    std::function<void(glm::vec3)>                      onFocusEntity;
    std::function<void()>                               onAssetsImport;
    std::function<void()>                               onCookShaders;
    std::function<void(std::filesystem::path)>          onProjectSelected;
};

} // namespace StellarAlia::Editor
