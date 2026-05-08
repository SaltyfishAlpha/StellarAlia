#include "ui/panels/ConsolePanel.hpp"
#include <imgui.h>
#include <cstdio>

namespace StellarAlia::Editor {

static const char* SourceLabel(DiagSource s) {
    switch (s) {
        case DiagSource::ShaderCook: return "ShaderCook";
        case DiagSource::Material:   return "Material";
        case DiagSource::Scene:      return "Scene";
        case DiagSource::Script:     return "Script";
        case DiagSource::Runtime:    return "Runtime";
    }
    return "?";
}

void ConsolePanel::OnDraw() {
    const auto& items = m_diags->All();
    const bool  hasNew = items.size() > m_lastCount;
    m_lastCount = items.size();

    // ── Toolbar ───────────────────────────────────────────────────────────────
    if (ImGui::Button("Clear")) {
        m_diags->Clear();
        m_lastCount = 0;
    }
    ImGui::SameLine();

    const int errN  = m_diags->ErrorCount();
    const int warnN = m_diags->WarningCount();

    // Toggle buttons styled by severity presence.
    {
        const ImVec4 on  = {0.65f, 0.15f, 0.15f, 1.f};
        const ImVec4 off = {0.25f, 0.25f, 0.25f, 1.f};
        ImGui::PushStyleColor(ImGuiCol_Button, errN ? on : off);
        char lbl[32]; std::snprintf(lbl, sizeof(lbl), "Errors %d", errN);
        if (ImGui::Button(lbl)) m_showErrors = !m_showErrors;
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    {
        const ImVec4 on  = {0.55f, 0.40f, 0.10f, 1.f};
        const ImVec4 off = {0.25f, 0.25f, 0.25f, 1.f};
        ImGui::PushStyleColor(ImGuiCol_Button, warnN ? on : off);
        char lbl[32]; std::snprintf(lbl, sizeof(lbl), "Warnings %d", warnN);
        if (ImGui::Button(lbl)) m_showWarnings = !m_showWarnings;
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    if (ImGui::Button("Info")) m_showInfo = !m_showInfo;

    ImGui::Separator();

    // ── Message list ──────────────────────────────────────────────────────────
    ImGui::BeginChild("##console_scroll", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    for (const auto& d : items) {
        if (d.level == DiagLevel::Error   && !m_showErrors)   continue;
        if (d.level == DiagLevel::Warning && !m_showWarnings) continue;
        if (d.level == DiagLevel::Info    && !m_showInfo)     continue;

        ImVec4      color;
        const char* prefix;
        switch (d.level) {
            case DiagLevel::Error:   color = {0.95f, 0.35f, 0.35f, 1.f}; prefix = "[ERR] "; break;
            case DiagLevel::Warning: color = {0.95f, 0.80f, 0.20f, 1.f}; prefix = "[WRN] "; break;
            default:                 color = {0.85f, 0.85f, 0.85f, 1.f}; prefix = "[INF] "; break;
        }

        ImGui::PushStyleColor(ImGuiCol_Text, color);

        std::string line = std::string(prefix)
                         + "[" + SourceLabel(d.source) + "] "
                         + d.message;
        if (!d.assetPath.empty())
            line += "  (" + d.assetPath.filename().string() + ")";

        ImGui::TextUnformatted(line.c_str());
        ImGui::PopStyleColor();

        // Show full path as tooltip on hover.
        if (!d.assetPath.empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", d.assetPath.string().c_str());
    }

    if (hasNew)
        ImGui::SetScrollHereY(1.0f);

    ImGui::EndChild();
}

} // namespace StellarAlia::Editor
