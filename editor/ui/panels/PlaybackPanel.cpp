#include "PlaybackPanel.hpp"

#include "engine/Application.hpp"
#include "engine/EnginePlayState.hpp"
#include "function/scene/Components.hpp"
#include "function/script/ScriptSystem.hpp"

#include <imgui.h>

#ifdef _WIN32
#  include <windows.h>
#  include <shellapi.h>
#endif

namespace StellarAlia::Editor {

void PlaybackPanel::OnDraw() {
    const EnginePlayState state = m_app->GetPlayState();

    // Require at least one camera and no unresolved errors before allowing play.
    const bool hasCam    = !m_app->GetScene().View<CameraComponent>().empty();
    const bool hasErrors = m_diags && m_diags->HasErrors();
    const bool canPlay   = hasCam && !hasErrors;

    switch (state) {
        case EnginePlayState::Editing:
            if (!canPlay) ImGui::BeginDisabled();
            if (ImGui::Button("Play"))
                m_presenter.RequestSetPlayState(EnginePlayState::Playing);
            if (!canPlay) {
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    if (hasErrors)
                        ImGui::SetTooltip("%d error(s) must be fixed before playing\n"
                                          "(see Console panel)",
                                          m_diags->ErrorCount());
                    else
                        ImGui::SetTooltip("Scene has no active camera");
                }
            }
            break;

        case EnginePlayState::Playing:
            if (ImGui::Button("Pause"))
                m_presenter.RequestSetPlayState(EnginePlayState::Paused);
            ImGui::SameLine();
            if (ImGui::Button("Stop"))
                m_presenter.RequestSetPlayState(EnginePlayState::Editing);
            break;

        case EnginePlayState::Paused:
            if (ImGui::Button("Resume"))
                m_presenter.RequestSetPlayState(EnginePlayState::Playing);
            ImGui::SameLine();
            if (ImGui::Button("Stop"))
                m_presenter.RequestSetPlayState(EnginePlayState::Editing);
            break;
    }

    ImGui::SameLine();
    switch (state) {
        case EnginePlayState::Editing: ImGui::TextDisabled("Edit");    break;
        case EnginePlayState::Playing: ImGui::TextColored({0.3f, 0.9f, 0.3f, 1.f}, "Playing"); break;
        case EnginePlayState::Paused:  ImGui::TextColored({0.9f, 0.7f, 0.1f, 1.f}, "Paused");  break;
    }

    if (!m_app->GetScriptSystem().IsAvailable()) {
        ImGui::SameLine();
        ImGui::Spacing();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.7f, 0.1f, 1.f));
        ImGui::Text("⚠ C# scripting unavailable");
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(".NET 8 Runtime not found.\n"
                              "Click to open the download page.");
            if (ImGui::IsItemClicked()) {
#ifdef _WIN32
                ShellExecuteW(nullptr, L"open",
                    L"https://dotnet.microsoft.com/download/dotnet/8.0",
                    nullptr, nullptr, SW_SHOWNORMAL);
#endif
            }
        }
    }
}

} // namespace StellarAlia::Editor
