#include "PlaybackPanel.hpp"

#include "engine/Application.hpp"
#include "engine/EnginePlayState.hpp"
#include "function/scene/Components.hpp"

#include <imgui.h>

namespace StellarAlia::Editor {

void PlaybackPanel::OnDraw() {
    const EnginePlayState state = m_app->GetPlayState();

    // Require an active camera entity in the scene before allowing play.
    bool hasCam = false;
    m_app->GetScene().View<CameraComponent, ActiveCameraTag>().each(
        [&](auto) { hasCam = true; });

    switch (state) {
        case EnginePlayState::Editing:
            if (!hasCam) ImGui::BeginDisabled();
            if (ImGui::Button("Play"))
                m_app->SetPlayState(EnginePlayState::Playing);
            if (!hasCam) {
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("Scene has no active camera");
            }
            break;

        case EnginePlayState::Playing:
            if (ImGui::Button("Pause"))
                m_app->SetPlayState(EnginePlayState::Paused);
            ImGui::SameLine();
            if (ImGui::Button("Stop"))
                m_app->SetPlayState(EnginePlayState::Editing);
            break;

        case EnginePlayState::Paused:
            if (ImGui::Button("Resume"))
                m_app->SetPlayState(EnginePlayState::Playing);
            ImGui::SameLine();
            if (ImGui::Button("Stop"))
                m_app->SetPlayState(EnginePlayState::Editing);
            break;
    }

    ImGui::SameLine();
    switch (state) {
        case EnginePlayState::Editing: ImGui::TextDisabled("Edit");    break;
        case EnginePlayState::Playing: ImGui::TextColored({0.3f, 0.9f, 0.3f, 1.f}, "Playing"); break;
        case EnginePlayState::Paused:  ImGui::TextColored({0.9f, 0.7f, 0.1f, 1.f}, "Paused");  break;
    }
}

} // namespace StellarAlia::Editor
