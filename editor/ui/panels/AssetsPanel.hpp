#pragma once

#include "ui/IEditorWindow.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace StellarAlia::Resource { class AssetRegistry; }

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// AssetsPanel — file browser for the active project's assets/ directory.
//
// Shows all files under projectDir/assets/ in a collapsible tree.
// .sameta sidecar files are hidden (they are implementation detail, not content).
// Single-click selects. Double-click on .sascene fires the load-scene callback.
//
// Import:
//   "Import…" toolbar button → file-path input modal → copies file into the
//   appropriate assets/ subdirectory and generates a .sameta sidecar.
//   Also responds to files dragged from Explorer (via GLFW drop callback):
//   call EnqueueDroppedPaths() from the GLFW drop callback each frame.
// ─────────────────────────────────────────────────────────────────────────────
class AssetsPanel : public IEditorWindow {
public:
    using SceneLoadCallback = std::function<void(const std::filesystem::path&)>;
    // Called after a new asset is imported so the registry can rescan.
    using ImportCallback    = std::function<void()>;

    explicit AssetsPanel(std::string projectDir,
                         std::string cookCacheDir,
                         Resource::AssetRegistry* registry = nullptr);

    std::string_view GetName() const override { return "Assets"; }
    void OnDraw() override;

    // Register a callback invoked when the user double-clicks a .sascene file.
    void SetSceneLoadCallback(SceneLoadCallback cb) { m_onSceneLoad = std::move(cb); }

    // Register a callback invoked after a successful asset import.
    void SetImportCallback(ImportCallback cb) { m_onImport = std::move(cb); }

    // Called from the GLFW drop callback to queue files for import.
    // Thread-safe for single-producer/single-consumer (both on main thread).
    void EnqueueDroppedPaths(int count, const char** paths);

    // Returns the currently selected asset path (empty = none).
    const std::filesystem::path& GetSelectedPath() const { return m_selectedPath; }

private:
    void DrawDirTree(const std::filesystem::path& dir);
    void DrawToolbar();
    void ProcessImportQueue();

    // Import a single file into the project.
    // Copies to the correct assets/ subdirectory, writes .sameta, cooks, returns true on success.
    bool ImportFile(const std::filesystem::path& srcPath);

    // Force-recook an already-imported asset (force=true).
    // Resolves type from the .sameta sidecar.  For .sanim, resolves the source
    // .glb path from the registry before delegating to CookAnimSidecar.
    void ReimportFile(const std::filesystem::path& srcPath);

    // Force-recook every asset under dir (recursive).
    void ReimportDir(const std::filesystem::path& dir);

    std::filesystem::path    m_assetsRoot;
    std::filesystem::path    m_selectedPath;
    std::string              m_projectDir;
    std::string              m_cookCacheDir;
    Resource::AssetRegistry* m_registry = nullptr;

    SceneLoadCallback m_onSceneLoad;
    ImportCallback    m_onImport;

    // ── Import modal state ─────────────────────────────────────────────────
    bool m_importModalOpen  = false;
    char m_importPathBuf[1024] = {};

    // ── Drag-and-drop queue ────────────────────────────────────────────────
    std::vector<std::filesystem::path> m_dropQueue;
};

} // namespace StellarAlia::Editor
