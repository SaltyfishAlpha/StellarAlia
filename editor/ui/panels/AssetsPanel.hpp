#pragma once

#include "ui/IEditorWindow.hpp"
#include "ui/presenters/AssetsPresenter.hpp"
#include "EditorDiagnostics.hpp"
#include "EditorContext.hpp"
#include "EditorSelection.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace StellarAlia          { class MaterialManager; }
namespace StellarAlia          { class InputSystem; }
namespace StellarAlia::Resource { class AssetRegistry; }
namespace StellarAlia::Editor   { class EditorIconCache; }
namespace StellarAlia::Editor   { class EntityTemplateRegistry; }

struct ImFont;

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
    AssetsPanel(EditorContext& ctx, AssetsPresenter& presenter);

    std::string_view GetName()    const override { return "Assets"; }
    ImGuiWindowFlags GetWindowFlags() const override { return 0; }
    void OnDraw() override;

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
    // Returns the directory currently shown in the right pane.
    const std::filesystem::path& GetCurrentDir()   const { return m_selectedDir; }

    // Marks the file pane for a rescan on the next draw (call after external file writes).
    void MarkFilePaneDirty() { m_filePaneDirty = true; }

    // Switch to a new project at runtime (called from EditorMode::LoadProject).
    void UpdateProjectDir(const std::filesystem::path& assetsRoot);

    // ── Top-bar actions (called from EditorUI menu bar) ───────────────────────
    void RequestImport();      // open the import-file modal on next draw
    void RequestRefresh();     // rescan registry immediately
    void RequestReimportAll(); // force-recook all assets under assetsRoot

private:
    enum class CreateKind : uint8_t { Mat, Saglsl, Scene, Script };

    // Left pane: recursive directory tree (dirs only).
    void DrawDirPane(const std::filesystem::path& dir);
    // Right pane: file list / grid for m_selectedDir.
    void DrawFilePane();

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

    // Force-recook an already-imported asset (force=true).
    // Resolves type from the .sameta sidecar.  For .sanim, resolves the source
    // .glb path from the registry before delegating to CookAnimSidecar.
    void ReimportFile(const std::filesystem::path& srcPath);

    // Force-recook every asset under dir (recursive).
    void ReimportDir(const std::filesystem::path& dir);

    // Sets m_selectedPath and syncs to EditorSelection.
    void SetSelectedPath(const std::filesystem::path& p);
    // Returns the import destination directory based on current selection.
    std::filesystem::path GetCurrentDestDir() const;

    AssetsPresenter&         m_presenter;

    std::filesystem::path    m_assetsRoot;
    std::filesystem::path    m_selectedDir;    // directory shown in the right pane
    std::filesystem::path    m_selectedPath;   // primary selection (for context ops)
    std::string              m_projectDir;
    std::string              m_cookCacheDir;
    Resource::AssetRegistry*  m_registry       = nullptr;
    MaterialManager*          m_matMgr         = nullptr;
    EditorDiagnostics*        m_diagnostics    = nullptr;
    InputSystem*              m_input          = nullptr;
    EditorSelection*          m_selectionCtx   = nullptr;
    EntityTemplateRegistry*   m_templateReg    = nullptr;

    // ── Multi-selection ────────────────────────────────────────────────────
    std::unordered_set<std::string>   m_selectedPaths;   // path strings of all selected files
    std::string                       m_shiftAnchorPath; // Shift+click range anchor
    std::string                       m_pendingDeselectOtherPath; // deferred single-select
    std::vector<std::filesystem::path> m_drawOrderFiles; // visible file order (prev frame)
    std::vector<std::filesystem::path> m_drawOrderFilesBuild; // accumulated this frame

    // ── Multi-delete batch ─────────────────────────────────────────────────
    std::vector<std::filesystem::path> m_pendingDeletePaths;
    bool                               m_batchDeleteConfirmOpen = false;

    std::function<void(const std::filesystem::path&)> m_onSceneLoad;
    std::function<void()>                             m_onImport;
    std::function<void()>                             m_onCookShaders;

    // ── Import dialog state ────────────────────────────────────────────────
    bool m_importModalOpen     = false;
    char m_importPathBuf[1024] = {};

    // ── Inline rename state (new files + explicit rename) ──────────────────
    std::filesystem::path m_renamingPath;
    char                  m_renameNameBuf[256] = {};
    bool                  m_renameFocusNext    = false;
    bool                  m_renamingFromTree   = false; // true = rename triggered from left tree

    // ── Delete confirmation ────────────────────────────────────────────────
    std::filesystem::path m_pendingDeletePath;
    bool                  m_deleteConfirmOpen = false;

    bool m_initialScanDone = false;

    // ── File-pane listing cache (avoids per-frame directory scan) ─────────────
    std::filesystem::path                          m_cachedScanDir;
    std::vector<std::filesystem::directory_entry>  m_cachedSubdirs;
    std::vector<std::filesystem::directory_entry>  m_cachedFiles;
    bool                                           m_filePaneDirty = true;

    // ── File icon display ──────────────────────────────────────────────────
    EditorIconCache* m_iconCache    = nullptr;
    ImFont*          m_iconFont     = nullptr;
    float            m_fileIconSize = 48.f;
    bool             m_gridView     = true;
};

} // namespace StellarAlia::Editor
