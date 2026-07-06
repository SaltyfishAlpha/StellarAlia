#include "ui/panels/PostProcessPanel.hpp"
#include "ui/drawers/DrawerHelpers.hpp"
#include "ui/drawers/ParamWidgets.hpp"
#include "resource/AssetRegistry.hpp"
#include "engine/Application.hpp"
#include "function/renderer/SceneRenderer.hpp"

#include <imgui.h>

#include <algorithm>

namespace StellarAlia::Editor {

namespace {
// Draw one ScreenEffect @Param (Issue #88): read the stored override or the
// schema default, draw the reflected widget, store back on change. Unchanged
// params stay absent from the map (fall back to defaults at resolve time).
bool DrawEffectParam(const ParamDef& def, std::map<std::string, ParamValue>& params) {
    const char* label = def.displayName.empty() ? def.name.c_str() : def.displayName.c_str();
    const auto  it    = params.find(def.name);
    ParamValue  v     = (it != params.end()) ? it->second : DefaultParamValue(def);
    if (DrawReflectedParam(def, v, label)) { params[def.name] = v; return true; }
    return false;
}
} // namespace

PostProcessPanel::PostProcessPanel(EditorContext& ctx, PostProcessPresenter& presenter)
    : m_presenter(presenter)
    , m_scene(ctx.scene)
    , m_registry(ctx.assetReg)
    , m_app(ctx.app)
{}

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

    // ── Depth of Field ────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Depth of Field")) {
        if (ImGui::Checkbox("Enabled##dof", &pp.dofEnabled))
            liveUpdate = true;
        ImGui::BeginDisabled(!pp.dofEnabled);
        if (ImGui::DragFloat("Focus Distance (m)##dof", &pp.focusDistance, 0.1f, 0.1f, 1000.f, "%.2f"))
            liveUpdate = true;
        if (ImGui::DragFloat("Aperture (f/)##dof", &pp.aperture, 0.1f, 0.5f, 22.f, "f/%.1f"))
            liveUpdate = true;
        if (ImGui::DragFloat("Focal Length (mm)##dof", &pp.focalLength, 1.f, 12.f, 200.f, "%.0f mm"))
            liveUpdate = true;
        if (ImGui::SliderInt("Samples##dof", &pp.dofSamples, 4, 32))
            liveUpdate = true;
        if (ImGui::SliderFloat("Max CoC (px)##dof", &pp.maxCocPx, 2.f, 40.f, "%.0f"))
            liveUpdate = true;
        ImGui::EndDisabled();
    }

    ImGui::Spacing();

    // ── Motion Blur (Camera Mode) ─────────────────────────────────────────────
    // Issue #46 Phase 1: covers camera pan/dolly/rotation only. Object & skinned
    // motion blur are Phase 2 — UI label flags the current mode explicitly.
    if (ImGui::CollapsingHeader("Camera Motion Blur")) {
        if (ImGui::Checkbox("Enabled##mb", &pp.motionBlurEnabled))
            liveUpdate = true;
        ImGui::BeginDisabled(!pp.motionBlurEnabled);
        if (ImGui::SliderFloat("Strength##mb",      &pp.motionBlurStrength, 0.0f, 2.0f, "%.2f"))
            liveUpdate = true;
        if (ImGui::SliderInt  ("Samples##mb",       &pp.motionBlurSamples,  4,    32))
            liveUpdate = true;
        if (ImGui::SliderFloat("Max Speed (NDC)##mb", &pp.motionBlurMaxSpeed, 0.01f, 0.3f, "%.3f"))
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

    ImGui::Spacing();

    // ── Screen Space Reflections (Issue #48) ─────────────────────────────────
    if (ImGui::CollapsingHeader("Screen Space Reflections")) {
        if (ImGui::Checkbox("Enabled##ssr", &pp.ssrEnabled))
            liveUpdate = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Enable TAA as well — it denoises the SSR ray-march jitter.");
        ImGui::BeginDisabled(!pp.ssrEnabled);
        if (ImGui::SliderFloat("Max Roughness##ssr", &pp.ssrMaxRoughness, 0.0f, 1.0f, "%.2f"))
            liveUpdate = true;
        if (ImGui::SliderInt  ("Max Steps##ssr",     &pp.ssrMaxSteps,    16,   128))
            liveUpdate = true;
        if (ImGui::SliderFloat("Thickness##ssr",     &pp.ssrThickness,   0.01f, 1.0f, "%.3f"))
            liveUpdate = true;
        if (ImGui::SliderFloat("Strength##ssr",      &pp.ssrStrength,    0.0f, 1.0f, "%.2f"))
            liveUpdate = true;
        ImGui::EndDisabled();
    }

    ImGui::Spacing();

    // ── Volumetric Fog (Issue #49) ───────────────────────────────────────────
    if (ImGui::CollapsingHeader("Volumetric Fog")) {
        if (ImGui::Checkbox("Enabled##volfog", &pp.volFogEnabled))
            liveUpdate = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Froxel single scattering; enable TAA to denoise the volume.");
        ImGui::BeginDisabled(!pp.volFogEnabled);
        if (ImGui::SliderFloat("Density##volfog",        &pp.volFogDensity,       0.0f,  0.5f,  "%.4f"))
            liveUpdate = true;
        if (ImGui::ColorEdit3 ("Albedo##volfog",         &pp.volFogAlbedo.x))
            liveUpdate = true;
        if (ImGui::SliderFloat("Anisotropy##volfog",     &pp.volFogAnisotropy,   -0.9f,  0.9f,  "%.2f"))
            liveUpdate = true;
        if (ImGui::SliderFloat("Distance##volfog",       &pp.volFogDistance,      8.0f,  256.f, "%.0f m"))
            liveUpdate = true;
        if (ImGui::SliderFloat("Height Base##volfog",    &pp.volFogHeightBase,   -50.f,  50.f,  "%.1f m"))
            liveUpdate = true;
        if (ImGui::SliderFloat("Height Falloff##volfog", &pp.volFogHeightFalloff, 0.0f,  1.0f,  "%.3f"))
            liveUpdate = true;
        if (ImGui::SliderFloat("Ambient##volfog",        &pp.volFogAmbient,       0.0f,  2.0f,  "%.2f"))
            liveUpdate = true;
        ImGui::EndDisabled();
    }

    ImGui::Spacing();

    // ── Screen modifications (Issue #47) ─────────────────────────────────────
    if (ImGui::CollapsingHeader("Vignette")) {
        if (ImGui::Checkbox("Enabled##vig", &pp.vignetteEnabled))
            liveUpdate = true;
        ImGui::BeginDisabled(!pp.vignetteEnabled);
        if (ImGui::SliderFloat("Intensity##vig",  &pp.vignetteIntensity,  0.f,   1.f,  "%.2f"))
            liveUpdate = true;
        if (ImGui::SliderFloat("Smoothness##vig", &pp.vignetteSmoothness, 0.01f, 1.f,  "%.2f"))
            liveUpdate = true;
        ImGui::EndDisabled();
    }

    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Chromatic Aberration")) {
        if (ImGui::Checkbox("Enabled##ca", &pp.caEnabled))
            liveUpdate = true;
        ImGui::BeginDisabled(!pp.caEnabled);
        if (ImGui::SliderFloat("Strength##ca", &pp.caStrength, 0.f, 5.f, "%.2f"))
            liveUpdate = true;
        ImGui::EndDisabled();
    }

    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Film Grain")) {
        if (ImGui::Checkbox("Enabled##grain", &pp.filmGrainEnabled))
            liveUpdate = true;
        ImGui::BeginDisabled(!pp.filmGrainEnabled);
        if (ImGui::SliderFloat("Intensity##grain", &pp.filmGrainIntensity, 0.f,  0.3f, "%.3f"))
            liveUpdate = true;
        if (ImGui::SliderFloat("Size##grain",      &pp.filmGrainSize,      0.5f, 5.f,  "%.2f"))
            liveUpdate = true;
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    ImGui::Separator();

    // ── Screen Effects (Issue #88) — per-scene custom .saeffect stack ───────────
    // Mirrors Unity Volume Overrides / UE Blendables: only effects added here run,
    // in list order within each injection point. "Add Effect" lists the cooked
    // catalog (SceneRenderer's ScreenEffectRegistry).
    if (m_app && ImGui::CollapsingHeader("Screen Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& fxReg = m_app->GetRenderer().GetScreenEffectRegistry();
        auto& list  = pp.screenEffects;

        int removeIdx = -1;
        for (int i = 0; i < static_cast<int>(list.size()); ++i) {
            ScreenEffectInstance&   se   = list[i];
            const ScreenEffectType* type = fxReg.Find(se.name);
            ImGui::PushID(i);

            if (ImGui::Checkbox("##en", &se.enabled)) liveUpdate = true;
            ImGui::SameLine();

            // Drag handle: canonical ImGui reorder (swap with neighbour while dragging).
            ImGui::Selectable(type ? type->name.c_str() : se.name.c_str(), false, 0, ImVec2(180, 0));
            if (ImGui::IsItemActive() && !ImGui::IsItemHovered()) {
                const int dir = ImGui::GetMouseDragDelta(0).y < 0.f ? -1 : 1;
                const int j   = i + dir;
                if (j >= 0 && j < static_cast<int>(list.size())) {
                    std::swap(list[i], list[j]);
                    ImGui::ResetMouseDragDelta();
                    liveUpdate = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) removeIdx = i;

            if (!type) {
                ImGui::TextColored(ImVec4(1.f, 0.5f, 0.3f, 1.f), "  effect not in catalog (cook missing?)");
            } else {
                ImGui::Indent();
                for (const auto& def : type->params)
                    if (DrawEffectParam(def, se.params)) liveUpdate = true;
                ImGui::Unindent();
            }
            ImGui::PopID();
        }
        if (removeIdx >= 0) { list.erase(list.begin() + removeIdx); liveUpdate = true; }

        ImGui::Spacing();
        if (ImGui::Button("Add Effect")) ImGui::OpenPopup("##add_effect");
        if (ImGui::BeginPopup("##add_effect")) {
            bool any = false;
            for (const auto& t : fxReg.All()) {
                const bool present = std::any_of(list.begin(), list.end(),
                    [&](const ScreenEffectInstance& s) { return s.name == t->name; });
                if (present) continue;
                any = true;
                if (ImGui::Selectable(t->name.c_str())) {
                    list.push_back(ScreenEffectInstance{ t->name, true, {} });
                    liveUpdate = true;
                }
            }
            if (!any) ImGui::TextDisabled("No effects available (cook a .saeffect)");
            ImGui::EndPopup();
        }
    }

    if (liveUpdate)
        m_presenter.RequestApply();
}

} // namespace StellarAlia::Editor
