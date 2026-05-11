#pragma once

#include "ui/presenters/IPresenter.hpp"
#include "EditorContext.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// AssetsPresenter — owns deferred import operations for AssetsPanel.
//
// Responsibility:
//   View (AssetsPanel::OnDraw)  → calls Request* / Enqueue*
//   Presenter (Update)          → runs nfd dialog, fs::copy_file, registry rescan
//
// Only the import path is here (step 4a/4b scope):
//   - NFD multi-file dialog
//   - File copy + .sameta generation + cook
//   - Drop-queue processing
//
// Other write ops (Create/Delete/Rename/Move/Reimport) remain in AssetsPanel
// because they don't use nfd or fs::copy_file and satisfy the criterion as-is.
// ─────────────────────────────────────────────────────────────────────────────
class AssetsPresenter final : public IPresenter {
public:
    explicit AssetsPresenter(EditorContext& ctx);
    void Update(float dt) override;

    // Called when the active project changes (mirrors AssetsPanel::UpdateProjectDir).
    void UpdateProjectDir(const std::filesystem::path& assetsRoot);

    // ── Import requests (called from AssetsPanel::OnDraw / callbacks) ──────────
    // Trigger the NFD multi-file dialog on next Update().
    // destDir: the directory to import into (and open the dialog at); empty = derive from selection.
    void RequestNFDImport(const std::filesystem::path& destDir = {});
    // Queue a single file for import (from GLFW drop callback or drag-and-drop).
    void EnqueueImport(const std::filesystem::path& srcPath);
    // Import a specific file into a known destination directory (from import modal).
    void RequestImportFile(const std::filesystem::path& srcPath,
                           const std::filesystem::path& destDir);

    // ── Results (Panel reads at top of OnDraw) ──────────────────────────────────
    // Returns true once after any import modifies the assets directory.
    bool ConsumeFilePaneDirty();

private:
    void RunNFDImport();
    bool RunImportFile(const std::filesystem::path& srcPath,
                       const std::filesystem::path& destDir);
    // Derive import destination from current EditorSelection (mirrors ImportFile's
    // m_selectedPath logic, but reads from shared selection state instead).
    std::filesystem::path GetCurrentDestDir() const;

    EditorContext&         m_ctx;
    std::filesystem::path  m_assetsRoot;
    std::string            m_cookCacheDir;

    bool          m_pendingNFDImport = false;
    std::filesystem::path m_pendingNFDDir;    // hint passed to RequestNFDImport

    // src  = source file path
    // dest = explicit destination dir; empty = derive from EditorSelection in Update()
    struct ImportOp { std::filesystem::path src, dest; };
    std::vector<ImportOp> m_importQueue;

    bool m_filePaneDirty = false;
};

} // namespace StellarAlia::Editor
