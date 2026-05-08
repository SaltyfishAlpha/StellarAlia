#pragma once

#include "ui/IEditorWindow.hpp"
#include "EditorDiagnostics.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace StellarAlia          { class MaterialManager; }
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
    using SceneLoadCallback    = std::function<void(const std::filesystem::path&)>;
    // Called after a new asset is imported so the registry can rescan.
    using ImportCallback       = std::function<void()>;
    // Called when the user requests a shader cook from the context menu.
    using CookShadersCallback  = std::function<void()>;

    explicit AssetsPanel(std::string projectDir,
                         std::string cookCacheDir,
                         Resource::AssetRegistry* registry = nullptr);

    std::string_view GetName()    const override { return "Assets"; }
    ImGuiWindowFlags GetWindowFlags() const override { return ImGuiWindowFlags_HorizontalScrollbar; }
    void OnDraw() override;

    // Register a callback invoked when the user double-clicks a .sascene file.
    void SetSceneLoadCallback(SceneLoadCallback cb) { m_onSceneLoad = std::move(cb); }

    // Register a callback invoked after a successful asset import.
    void SetImportCallback(ImportCallback cb) { m_onImport = std::move(cb); }

    // Register a callback invoked when the user selects "Cook Shader" on a .saglsl file.
    void SetCookShadersCallback(CookShadersCallback cb) { m_onCookShaders = std::move(cb); }

    // Scans the assets root: generates missing .sameta, cooks materials, then
    // rescans the registry.  Call from EditorMode::OnAttach (after SetImportCallback)
    // so the registry is populated before the first UI frame.  OnDraw retries as a
    // fallback in case the panel was not yet visible during OnAttach.
    void RunInitialScan();

    // Called from the GLFW drop callback to queue files for import.
    // Thread-safe for single-producer/single-consumer (both on main thread).
    void EnqueueDroppedPaths(int count, const char** paths);

    // Returns the currently selected asset path (empty = none).
    const std::filesystem::path& GetSelectedPath() const { return m_selectedPath; }

    // Optional wiring — enables type-registration guard in CreateMatFromShader
    // and diagnostic reporting for cook/material errors.
    void SetMaterialManager(MaterialManager* mm)    { m_matMgr      = mm; }
    void SetDiagnostics(EditorDiagnostics* diags)   { m_diagnostics = diags; }

    // ── Top-bar actions (called from EditorUI menu bar) ───────────────────────
    void RequestImport();      // open the import-file modal on next draw
    void RequestRefresh();     // rescan registry immediately
    void RequestReimportAll(); // force-recook all assets under assetsRoot

private:
    enum class CreateKind : uint8_t { Mat, Saglsl };

    void DrawDirTree(const std::filesystem::path& dir);
    void ProcessImportQueue();

    // Write template content + .sameta + cook, then enter inline rename.
    // Default filenames: "New Material" / "New Shader".
    void CreateNewFile(CreateKind kind, const std::filesystem::path& dir);

    // Create a new empty directory and enter inline rename.
    void CreateNewDir(const std::filesystem::path& parent);

    // Create a .mat from a .saglsl shader, then enter inline rename.
    void CreateMatFromShader(const std::string& typeName,
                             const std::filesystem::path& dir,
                             const std::string& baseName,
                             const std::string& defaultParamsJson = {});

    // Rename m_renamingPath to m_renameNameBuf + original extension.
    void CommitRename();

    // Delete a file (+ .sameta) or an entire directory tree; rescans registry.
    void DeletePath(const std::filesystem::path& path);

    // Move src file or directory into destDir; updates .sameta and selection.
    void MoveAsset(const std::filesystem::path& src,
                   const std::filesystem::path& destDir);

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
    Resource::AssetRegistry* m_registry     = nullptr;
    MaterialManager*         m_matMgr       = nullptr;
    EditorDiagnostics*       m_diagnostics  = nullptr;

    SceneLoadCallback    m_onSceneLoad;
    ImportCallback       m_onImport;
    CookShadersCallback  m_onCookShaders;

    // ── Import dialog state ────────────────────────────────────────────────
    // NFD path: flag set by RequestImport(), consumed at the top of OnDraw().
    // Fallback (no nfd): text-input modal.
    bool m_importDialogPending = false;
    bool m_importModalOpen     = false;
    char m_importPathBuf[1024] = {};

    void ProcessNFDImport();

    // ── Inline rename state (new files + explicit rename) ──────────────────
    std::filesystem::path m_renamingPath;
    char                  m_renameNameBuf[256] = {};
    bool                  m_renameFocusNext    = false;

    // ── Drag-and-drop queue ────────────────────────────────────────────────
    std::vector<std::filesystem::path> m_dropQueue;

    // ── Delete confirmation ────────────────────────────────────────────────
    std::filesystem::path m_pendingDeletePath;
    bool                  m_deleteConfirmOpen = false;

    bool m_initialScanDone = false;
};

} // namespace StellarAlia::Editor
