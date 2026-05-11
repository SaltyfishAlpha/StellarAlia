#pragma once

#include "ui/presenters/ProjectBrowserPresenter.hpp"
#include "project/ProjectManager.hpp"
#include "EditorContext.hpp"

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

    ProjectBrowserPanel(EditorContext& ctx, ProjectBrowserPresenter& presenter);

    // Queue the modal to open on the next OnDraw call.
    void Open() { m_pendingOpen = true; }

    // Call each frame after ImGui::NewFrame().
    void OnDraw();

private:
    ProjectBrowserPresenter& m_presenter;
    ProjectManager&          m_mgr;
    ProjectSelectedCallback  m_onSelected;

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
