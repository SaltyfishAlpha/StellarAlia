#include "ui/panels/ProjectBrowserPanel.hpp"

#include "core/logs/Log.hpp"

#include <imgui.h>

#if __has_include(<nfd.h>)
#include <nfd.h>
#define SA_HAS_NFD 1
#endif

#include <cstring>
#include <filesystem>

namespace StellarAlia::Editor {

namespace fs = std::filesystem;

ProjectBrowserPanel::ProjectBrowserPanel(EditorContext& ctx, ProjectBrowserPresenter& presenter)
    : m_presenter(presenter)
    , m_mgr(*ctx.projectMgr)
    , m_onSelected(ctx.onProjectSelected)
{}

void ProjectBrowserPanel::OnDraw() {
    if (m_pendingOpen) {
        ImGui::OpenPopup("Project Manager");
        m_pendingOpen = false;
    }

    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                             ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(660, 0), ImGuiCond_Always);

    if (ImGui::BeginPopupModal("Project Manager", nullptr,
                               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        DrawModal();
        ImGui::EndPopup();
    }
}

void ProjectBrowserPanel::DrawModal() {
    m_mgr.RemoveStaleRecents();

    DrawCreateSection();
    ImGui::Spacing();
    DrawOpenSection();
    ImGui::Spacing();
    DrawRecentSection();
}

void ProjectBrowserPanel::DrawCreateSection() {
    // Consume create result from previous frame.
    {
        fs::path created;
        if (m_presenter.ConsumeCreateSuccess(created)) {
            m_statusMessage.clear();
            ImGui::CloseCurrentPopup();
            if (m_onSelected) m_onSelected(created);
            return;
        }
        std::string err;
        if (m_presenter.ConsumeCreateError(err)) {
            m_statusMessage = std::move(err);
            m_statusIsError = true;
        }
    }

    if (!ImGui::CollapsingHeader("Create New Project", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    ImGui::PushItemWidth(380.f);
    ImGui::InputText("Name", m_newName, sizeof(m_newName));
    ImGui::InputText("Location", m_newDir, sizeof(m_newDir));
    ImGui::PopItemWidth();

    ImGui::SameLine();
    if (ImGui::Button("Browse..."))
        PickDirectory();

    ImGui::Text("Template:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Empty",   m_selectedTmpl == ProjectTemplate::Empty))
        m_selectedTmpl = ProjectTemplate::Empty;
    ImGui::SameLine();
    if (ImGui::RadioButton("Default", m_selectedTmpl == ProjectTemplate::Default))
        m_selectedTmpl = ProjectTemplate::Default;

    const bool canCreate = m_newName[0] != '\0' && m_newDir[0] != '\0';
    ImGui::BeginDisabled(!canCreate);
    const float btnW = 140.f;
    ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - btnW + ImGui::GetCursorPosX());
    if (ImGui::Button("Create Project", ImVec2(btnW, 0)))
        m_presenter.RequestCreateProject(fs::path(m_newDir), m_newName, m_selectedTmpl);
    ImGui::EndDisabled();

    if (!m_statusMessage.empty()) {
        const ImVec4 col = m_statusIsError ? ImVec4(0.9f, 0.3f, 0.3f, 1.f)
                                           : ImVec4(0.3f, 0.9f, 0.3f, 1.f);
        ImGui::TextColored(col, "%s", m_statusMessage.c_str());
    }
}

void ProjectBrowserPanel::DrawOpenSection() {
    if (!ImGui::CollapsingHeader("Open Existing Project", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    if (ImGui::Button("Open .saproject File...")) {
#ifdef SA_HAS_NFD
        if (NFD_Init() == NFD_OKAY) {
            static constexpr nfdfilteritem_t kFilter[] = {
                { "StellarAlia Project", "saproject" }
            };
            nfdchar_t* outPath = nullptr;
            const nfdresult_t res = NFD_OpenDialogU8(&outPath, kFilter, 1, nullptr);
            if (res == NFD_OKAY && outPath) {
                const fs::path chosen(outPath);
                NFD_FreePathU8(outPath);
                NFD_Quit();
                ImGui::CloseCurrentPopup();
                if (m_onSelected) m_onSelected(chosen);
                return;
            } else if (res == NFD_ERROR) {
                SA_LOG_WARN("ProjectBrowserPanel: NFD open error: {}", NFD_GetError());
            }
            NFD_Quit();
        }
#else
        SA_LOG_WARN("ProjectBrowserPanel: NFD not available — cannot open file dialog");
#endif
    }
}

void ProjectBrowserPanel::DrawRecentSection() {
    const auto& recents = m_mgr.GetRecents();
    if (recents.empty()) return;

    if (!ImGui::CollapsingHeader("Recent Projects", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    fs::path toOpen;
    fs::path toRemove;

    for (const auto& rp : recents) {
        const std::string label = rp.name + "##" + rp.saprojectPath.string();
        if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowOverlap)) {
            toOpen = rp.saprojectPath;
        }
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 22.f);
        const std::string xLabel = "x##" + rp.saprojectPath.string();
        if (ImGui::SmallButton(xLabel.c_str()))
            toRemove = rp.saprojectPath;

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.f));
        ImGui::TextUnformatted(rp.saprojectPath.string().c_str());
        ImGui::PopStyleColor();
    }

    if (!toRemove.empty())
        m_mgr.RemoveRecent(toRemove);

    if (!toOpen.empty()) {
        ImGui::CloseCurrentPopup();
        if (m_onSelected) m_onSelected(toOpen);
    }
}

void ProjectBrowserPanel::PickDirectory() {
#ifdef SA_HAS_NFD
    if (NFD_Init() != NFD_OKAY) {
        SA_LOG_WARN("ProjectBrowserPanel: NFD_Init failed: {}", NFD_GetError());
        return;
    }
    nfdchar_t* outPath = nullptr;
    const nfdresult_t res = NFD_PickFolderU8(&outPath, nullptr);
    if (res == NFD_OKAY && outPath) {
        std::strncpy(m_newDir, outPath, sizeof(m_newDir) - 1);
        m_newDir[sizeof(m_newDir) - 1] = '\0';
        NFD_FreePathU8(outPath);
    } else if (res == NFD_ERROR) {
        SA_LOG_WARN("ProjectBrowserPanel: folder picker error: {}", NFD_GetError());
    }
    NFD_Quit();
#endif
}

} // namespace StellarAlia::Editor
