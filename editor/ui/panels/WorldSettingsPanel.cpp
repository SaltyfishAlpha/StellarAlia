#include "ui/panels/WorldSettingsPanel.hpp"
#include "ui/ComponentDrawers.hpp"
#include "resource/AssetRegistry.hpp"

#include <imgui.h>

namespace StellarAlia::Editor {

void WorldSettingsPanel::OnDraw() {
    WorldSettings& ws = m_scene->GetWorldSettings();

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
                m_renderer->ApplyWorldSettings(ws, /*updateIBL=*/false);
            }
        } else {
            // HDR asset picker — changing it clears stale baked products.
            AssetID prevHdr = ws.skyboxHdr;
            if (DrawAssetIDField("HDR Asset", ws.skyboxHdr, "Texture", m_registry)) {
                if (ws.skyboxHdr != prevHdr) {
                    ws.sh9 = ws.prefilteredEnv = ws.brdfLut = ws.skyboxCubemap = AssetID::Invalid();
                }
            }

            const bool hasBaked = ws.sh9.IsValid() && ws.prefilteredEnv.IsValid()
                                  && ws.brdfLut.IsValid() && ws.skyboxCubemap.IsValid();
            ImGui::LabelText("IBL Status", hasBaked ? "Baked" : "Not baked");

            // Bake IBL — enabled only when an HDR is set and not yet baked.
            const bool canBake = ws.skyboxHdr.IsValid() && !hasBaked;
            if (!canBake) ImGui::BeginDisabled();
            if (ImGui::Button("Bake IBL"))
                m_renderer->ApplyWorldSettings(ws);
            if (!canBake) {
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip(hasBaked ? "IBL already baked — use Re-bake to redo"
                                              : "Set a skybox HDR asset first");
            }

            // Re-bake — deletes cached files and forces a fresh GPU bake.
            if (hasBaked) {
                ImGui::SameLine();
                if (ImGui::Button("Re-bake"))
                    m_renderer->RebakeIBL(ws);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Delete cached IBL textures and bake again from the source HDR");
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
    ImGui::Separator();
    ImGui::Spacing();

    // ── Apply (IBL bake / settings reload) ───────────────────────────────────
    if (ImGui::Button("Apply Settings", { -1.f, 0.f }))
        m_renderer->ApplyWorldSettings(ws);
}

} // namespace StellarAlia::Editor
