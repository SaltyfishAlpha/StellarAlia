#include "ui/panels/WorldSettingsPanel.hpp"

#include <imgui.h>

namespace StellarAlia::Editor {

// UUID of assets/textures/builtin/color_grading_lut_blue.png — used as the
// default test LUT when the user first enables LUT tonemap mode from the UI.
static constexpr const char* kDefaultLutUUID = "911aa46e-4510-4bab-bc15-0013ab7ac82f";

void WorldSettingsPanel::OnDraw() {
    WorldSettings& ws = m_scene->GetWorldSettings();
    bool liveUpdate = false;  // true when a cheap change should preview immediately

    // ── Background ────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Background", ImGuiTreeNodeFlags_DefaultOpen)) {
        int bgMode = (ws.backgroundMode == WorldSettings::BackgroundMode::Skybox) ? 1 : 0;
        if (ImGui::RadioButton("Solid Color", &bgMode, 0))
            ws.backgroundMode = WorldSettings::BackgroundMode::SolidColor;
        ImGui::SameLine();
        if (ImGui::RadioButton("Skybox", &bgMode, 1))
            ws.backgroundMode = WorldSettings::BackgroundMode::Skybox;

        if (ws.backgroundMode == WorldSettings::BackgroundMode::SolidColor) {
            float col[3] = { ws.backgroundColor.r, ws.backgroundColor.g, ws.backgroundColor.b };
            if (ImGui::ColorEdit3("Color (linear)", col,
                    ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR)) {
                ws.backgroundColor = { col[0], col[1], col[2] };
                liveUpdate = true;
            }
        } else {
            ImGui::LabelText("HDR Asset",
                ws.skyboxHdr.IsValid() ? ws.skyboxHdr.ToString().c_str() : "(none)");

            const bool hasBaked = ws.sh9.IsValid() && ws.prefilteredEnv.IsValid()
                                  && ws.brdfLut.IsValid() && ws.skyboxCubemap.IsValid();
            ImGui::LabelText("IBL Status", hasBaked ? "Baked" : "Not baked");

            // Button enabled only when there is a source HDR but no baked products yet.
            const bool canBake = ws.skyboxHdr.IsValid() && !hasBaked;
            if (!canBake) ImGui::BeginDisabled();
            if (ImGui::Button("Bake IBL"))
                m_renderer->ApplyWorldSettings(ws);
            if (!canBake) {
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip(hasBaked ? "IBL already baked"
                                              : "Set a skybox HDR asset first");
            }

            if (hasBaked && ImGui::TreeNode("Baked Assets (read-only)")) {
                ImGui::LabelText("SH9",             ws.sh9.ToString().c_str());
                ImGui::LabelText("Prefiltered Env", ws.prefilteredEnv.ToString().c_str());
                ImGui::LabelText("BRDF LUT",        ws.brdfLut.ToString().c_str());
                ImGui::LabelText("Skybox Cubemap",  ws.skyboxCubemap.ToString().c_str());
                ImGui::TreePop();
            }
        }
    }

    ImGui::Spacing();

    // ── Tonemap ───────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Tonemap", ImGuiTreeNodeFlags_DefaultOpen)) {
        int tmMode = (ws.tonemapMode == WorldSettings::TonemapMode::LUT) ? 1 : 0;
        if (ImGui::RadioButton("ACES (Builtin)", &tmMode, 0))
            ws.tonemapMode = WorldSettings::TonemapMode::Builtin;
        ImGui::SameLine();
        if (ImGui::RadioButton("LUT", &tmMode, 1)) {
            ws.tonemapMode = WorldSettings::TonemapMode::LUT;
            // Pre-populate with the builtin test LUT so the renderer has a valid
            // texture on the first Apply — user can replace it later.
            if (!ws.tonemapLut.IsValid())
                ws.tonemapLut = AssetID::FromString(kDefaultLutUUID);
        }

        if (ImGui::SliderFloat("Exposure", &ws.exposure, 0.1f, 10.f, "%.2f",
                ImGuiSliderFlags_Logarithmic))
            liveUpdate = true;

        if (ws.tonemapMode == WorldSettings::TonemapMode::Builtin) {
            if (ImGui::SliderFloat("Gamma", &ws.gamma, 1.f, 3.f, "%.2f"))
                liveUpdate = true;
        } else {
            ImGui::LabelText("LUT Asset",
                ws.tonemapLut.IsValid() ? ws.tonemapLut.ToString().c_str() : "(none)");
            if (ImGui::SliderFloat("LUT Strength", &ws.lutStrength, 0.f, 1.f, "%.2f"))
                liveUpdate = true;
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Apply ─────────────────────────────────────────────────────────────────
    if (ImGui::Button("Apply Settings", { -1.f, 0.f }))
        m_renderer->ApplyWorldSettings(ws);
    else if (liveUpdate)
        m_renderer->ApplyWorldSettings(ws, /*updateIBL=*/false);
}

} // namespace StellarAlia::Editor
