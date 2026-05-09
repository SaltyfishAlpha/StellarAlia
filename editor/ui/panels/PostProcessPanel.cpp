#include "ui/panels/PostProcessPanel.hpp"
#include "ui/ComponentDrawers.hpp"
#include "resource/AssetRegistry.hpp"

#include <imgui.h>

namespace StellarAlia::Editor {

void PostProcessPanel::OnDraw() {
    WorldSettings& ws = m_scene->GetWorldSettings();
    PostProcessSettings& pp = ws.pp;
    bool liveUpdate = false;

    // ── Bloom ─────────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Bloom", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Checkbox("Enabled##bloom", &pp.bloomEnabled))
            liveUpdate = true;
        ImGui::BeginDisabled(!pp.bloomEnabled);
        if (ImGui::SliderFloat("Threshold", &pp.bloomThreshold, 0.f, 4.f, "%.2f"))
            liveUpdate = true;
        if (ImGui::SliderFloat("Strength",  &pp.bloomStrength,  0.f, 2.f, "%.2f"))
            liveUpdate = true;
        if (ImGui::SliderFloat("Radius",    &pp.bloomRadius,    0.1f, 2.f, "%.2f"))
            liveUpdate = true;
        ImGui::EndDisabled();
    }

    ImGui::Spacing();

    // ── Tonemap ───────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Tonemap", ImGuiTreeNodeFlags_DefaultOpen)) {
        int tmMode = (pp.tonemapMode == PostProcessSettings::TonemapMode::LUT) ? 1 : 0;
        if (ImGui::RadioButton("ACES (Builtin)", &tmMode, 0)) {
            pp.tonemapMode = PostProcessSettings::TonemapMode::Builtin;
            liveUpdate = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("LUT", &tmMode, 1)) {
            pp.tonemapMode = PostProcessSettings::TonemapMode::LUT;
            liveUpdate = true;
        }

        if (ImGui::SliderFloat("Exposure", &pp.exposure, 0.1f, 10.f, "%.2f",
                ImGuiSliderFlags_Logarithmic))
            liveUpdate = true;

        if (pp.tonemapMode == PostProcessSettings::TonemapMode::Builtin) {
            if (ImGui::SliderFloat("Gamma", &pp.gamma, 1.f, 3.f, "%.2f"))
                liveUpdate = true;
        } else {
            if (DrawAssetIDField("LUT Asset", pp.tonemapLut, "Texture", m_registry))
                liveUpdate = true;
            if (ImGui::SliderFloat("LUT Strength", &pp.lutStrength, 0.f, 1.f, "%.2f"))
                liveUpdate = true;
        }
    }

    if (liveUpdate)
        m_renderer->ApplyWorldSettings(ws, /*updateIBL=*/false);
}

} // namespace StellarAlia::Editor
