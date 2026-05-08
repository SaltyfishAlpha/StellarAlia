#pragma once

#include "project/ProjectManager.hpp"

#include <filesystem>
#include <functional>
#include <string>

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// ProjectBrowserPanel — fullscreen modal for creating and opening projects.
//
// Not registered with EditorUI; EditorMode drives it directly from OnRenderUI.
// Call Open() once to trigger the modal on the next ImGui frame.
// ─────────────────────────────────────────────────────────────────────────────
class ProjectBrowserPanel {
public:
    using ProjectSelectedCallback = std::function<void(std::filesystem::path saprojectPath)>;

    ProjectBrowserPanel(ProjectManager& mgr,
                        const std::filesystem::path& engineAssetsDir);

    void SetOnProjectSelected(ProjectSelectedCallback cb) { m_onSelected = std::move(cb); }

    // Queue the modal to open on the next OnDraw call.
    void Open() { m_pendingOpen = true; }

    // Call each frame after ImGui::NewFrame().
    void OnDraw();

private:
    ProjectManager&         m_mgr;
    std::filesystem::path   m_engineAssetsDir;
    ProjectSelectedCallback m_onSelected;

    bool            m_pendingOpen   = false;
    char            m_newName[256]  = {};
    char            m_newDir[1024]  = {};
    ProjectTemplate m_selectedTmpl  = ProjectTemplate::Default;
    std::string     m_statusMessage;
    bool            m_statusIsError = false;

    void DrawModal();
    void DrawCreateSection();
    void DrawOpenSection();
    void DrawRecentSection();
    void PickDirectory();
};

} // namespace StellarAlia::Editor
