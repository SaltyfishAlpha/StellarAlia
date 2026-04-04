#include "ui/panels/SettingsPanel.hpp"

#include <imgui.h>

namespace StellarAlia::Editor {

void SettingsPanel::OnDraw() {
    ImGuiIO& io = ImGui::GetIO();

    // ── UI scale ──────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("UI", ImGuiTreeNodeFlags_DefaultOpen)) {
        float scale = io.FontGlobalScale;
        if (ImGui::SliderFloat("Font Scale", &scale, 0.5f, 3.0f, "%.1fx"))
            io.FontGlobalScale = scale;
        ImGui::SameLine();
        if (ImGui::Button("Reset##font"))
            io.FontGlobalScale = 1.0f;
    }

    // ── Display info ──────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Viewport: %.0f x %.0f", io.DisplaySize.x, io.DisplaySize.y);
        ImGui::Text("Frame time: %.2f ms  (%.0f FPS)",
                    1000.0f / io.Framerate, io.Framerate);
    }
}

} // namespace StellarAlia::Editor
