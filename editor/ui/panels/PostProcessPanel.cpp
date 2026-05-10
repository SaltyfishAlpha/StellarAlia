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
        if (ImGui::SliderInt("Mip Levels##bloom", &pp.bloomMipLevels, 2, 8))
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

    // ── Auto Exposure ─────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Auto Exposure")) {
        if (ImGui::Checkbox("Enabled##ae", &pp.autoExposureEnabled))
            liveUpdate = true;
        ImGui::BeginDisabled(!pp.autoExposureEnabled);
        if (ImGui::SliderFloat("EV Min##ae",      &pp.aeEvMin,       -8.f, 0.f,  "%.1f"))
            liveUpdate = true;
        if (ImGui::SliderFloat("EV Max##ae",      &pp.aeEvMax,        0.f, 8.f,  "%.1f"))
            liveUpdate = true;
        if (ImGui::SliderFloat("Adapt Speed##ae", &pp.aeAdaptSpeed,   0.1f, 10.f, "%.2f"))
            liveUpdate = true;
        if (ImGui::SliderFloat("Low %##ae",       &pp.aeLowPercent,   0.f,  0.5f, "%.2f"))
            liveUpdate = true;
        if (ImGui::SliderFloat("High %##ae",      &pp.aeHighPercent,  0.5f, 1.0f, "%.2f"))
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

        ImGui::BeginDisabled(pp.autoExposureEnabled);
        if (ImGui::SliderFloat("Exposure", &pp.exposure, 0.1f, 10.f, "%.2f",
                ImGuiSliderFlags_Logarithmic))
            liveUpdate = true;
        ImGui::EndDisabled();

        if (pp.tonemapMode == PostProcessSettings::TonemapMode::Builtin) {
            ImGui::Spacing();
            if (ImGui::CollapsingHeader("Color Grading")) {
                ColorGradingSettings& cg = pp.colorGrading;
                if (ImGui::Checkbox("Enabled##cg", &cg.enabled))
                    liveUpdate = true;
                ImGui::BeginDisabled(!cg.enabled);
                if (ImGui::ColorEdit3("Lift##cg",    &cg.lift.x,    ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
                    liveUpdate = true;
                if (ImGui::ColorEdit3("Midtone##cg", &cg.midtone.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
                    liveUpdate = true;
                if (ImGui::ColorEdit3("Gain##cg",    &cg.gain.x,    ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
                    liveUpdate = true;
                if (ImGui::SliderFloat("Saturation##cg", &cg.saturation, 0.f, 3.f, "%.2f"))
                    liveUpdate = true;
                if (ImGui::SliderFloat("Contrast##cg",   &cg.contrast,   0.f, 3.f, "%.2f"))
                    liveUpdate = true;
                ImGui::EndDisabled();
            }
        } else {
            if (DrawAssetIDField("LUT Asset", pp.tonemapLut, "Texture", m_registry))
                liveUpdate = true;
            if (ImGui::SliderFloat("LUT Strength", &pp.lutStrength, 0.f, 1.f, "%.2f"))
                liveUpdate = true;
        }
    }

    ImGui::Spacing();

    // ── SSAO (GTAO) ───────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Ambient Occlusion (GTAO)")) {
        if (ImGui::Checkbox("Enabled##ssao", &pp.ssaoEnabled))
            liveUpdate = true;
        ImGui::BeginDisabled(!pp.ssaoEnabled);
        if (ImGui::SliderFloat("Radius (px)##ssao",&pp.ssaoRadius,        4.f,  128.f,"%.0f"))
            liveUpdate = true;
        if (ImGui::SliderFloat("Strength##ssao",  &pp.ssaoStrength,      0.f,  2.f,  "%.2f"))
            liveUpdate = true;
        if (ImGui::SliderFloat("Bias##ssao",      &pp.ssaoBias,          0.f,  0.1f, "%.4f"))
            liveUpdate = true;
        if (ImGui::SliderInt  ("Directions##ssao",&pp.ssaoDirections,    2,    16))
            liveUpdate = true;
        if (ImGui::SliderInt  ("Steps##ssao",     &pp.ssaoSteps,         2,    8))
            liveUpdate = true;
        if (ImGui::SliderFloat("Blur Sharpness",  &pp.ssaoBlurSharpness, 1.f,  50.f, "%.1f"))
            liveUpdate = true;
        ImGui::EndDisabled();
    }

    ImGui::Spacing();

    // ── TAA (Temporal AA) ─────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Temporal Anti-Aliasing (TAA)")) {
        if (ImGui::Checkbox("Enabled##taa", &pp.taaEnabled))
            liveUpdate = true;
        ImGui::BeginDisabled(!pp.taaEnabled);
        if (ImGui::SliderFloat("Static Blend##taa",  &pp.taaBlendStatic, 0.01f, 0.5f,  "%.3f"))
            liveUpdate = true;
        if (ImGui::SliderFloat("Motion Blend##taa",  &pp.taaBlendMotion, 0.1f,  1.0f,  "%.2f"))
            liveUpdate = true;
        if (ImGui::Checkbox("Anti-Ghosting##taa", &pp.taaAntiGhosting))
            liveUpdate = true;
        ImGui::EndDisabled();
    }

    if (liveUpdate)
        m_renderer->ApplyWorldSettings(ws, /*updateIBL=*/false);
}

} // namespace StellarAlia::Editor
