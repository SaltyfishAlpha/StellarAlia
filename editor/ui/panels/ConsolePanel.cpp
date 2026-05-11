#include "ui/panels/ConsolePanel.hpp"
#include <imgui.h>
#include <cstdio>
#include <string>

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
    if (ImGui::BeginTabBar("##console_tabs")) {
        DrawDiagnosticsTab();
        DrawEngineLogsTab();
        ImGui::EndTabBar();
    }
}

void ConsolePanel::DrawDiagnosticsTab() {
    if (!ImGui::BeginTabItem("Diagnostics"))
        return;

    const auto& items  = m_diags->All();
    const bool  hasNew = items.size() > m_lastDiagCount
                      || m_presenter.HasNew();
    m_lastDiagCount = items.size();

    // ── Toolbar ───────────────────────────────────────────────────────────────
    if (ImGui::Button("Clear")) {
        m_diags->Clear();
        m_presenter.ClearScript();
        m_lastDiagCount = 0;
    }
    ImGui::SameLine();

    const int errN  = m_diags->ErrorCount();
    const int warnN = m_diags->WarningCount();

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
    ImGui::BeginChild("##diag_scroll", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    // EditorDiagnostics entries (shader cook, material, scene…)
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
        if (!d.assetPath.empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", d.assetPath.string().c_str());
    }

    // Script log entries (auto-routed via "script" named logger)
    const auto& scriptEntries = m_presenter.GetScriptEntries();
    if (!scriptEntries.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("--- Script ---");
        for (const auto& e : scriptEntries) {
            const int lvl = static_cast<int>(e.level);
            ImVec4      color;
            const char* prefix;
            if (lvl >= 4) {        // err / critical
                if (!m_showErrors) continue;
                color  = {0.95f, 0.35f, 0.35f, 1.f};
                prefix = "[ERR] ";
            } else if (lvl == 3) { // warn
                if (!m_showWarnings) continue;
                color  = {0.95f, 0.80f, 0.20f, 1.f};
                prefix = "[WRN] ";
            } else {               // trace/debug/info
                if (!m_showInfo) continue;
                color  = {0.85f, 0.85f, 0.85f, 1.f};
                prefix = "[INF] ";
            }
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            std::string line = std::string(prefix) + e.message;
            ImGui::TextUnformatted(line.c_str());
            ImGui::PopStyleColor();
        }
    }

    if (hasNew)
        ImGui::SetScrollHereY(1.0f);

    ImGui::EndChild();
    ImGui::EndTabItem();
}

void ConsolePanel::DrawEngineLogsTab() {
    const auto& entries  = m_presenter.GetEngineEntries();
    const bool  gotNew   = m_presenter.HasNew();
    const int   unread   = m_presenter.UnreadCount();

    char tabLabel[40];
    if (unread > 0)
        std::snprintf(tabLabel, sizeof(tabLabel), "Engine Logs [%d]###EngLogs", unread);
    else
        std::snprintf(tabLabel, sizeof(tabLabel), "Engine Logs###EngLogs");

    if (!ImGui::BeginTabItem(tabLabel))
        return;
    m_presenter.ResetUnread();

    // ── Toolbar ───────────────────────────────────────────────────────────────
    if (ImGui::Button("Clear"))
        m_presenter.ClearEngine();
    ImGui::SameLine();
    ImGui::TextUnformatted("Level:");

    struct LevelInfo { const char* label; ImVec4 color; };
    static constexpr LevelInfo kLevels[6] = {
        { "T", {0.55f, 0.55f, 0.55f, 1.f} },
        { "D", {0.70f, 0.70f, 0.70f, 1.f} },
        { "I", {0.85f, 0.85f, 0.85f, 1.f} },
        { "W", {0.95f, 0.80f, 0.20f, 1.f} },
        { "E", {0.95f, 0.35f, 0.35f, 1.f} },
        { "C", {1.00f, 0.20f, 0.20f, 1.f} },
    };
    for (int i = 0; i < 6; ++i) {
        ImGui::SameLine();
        const ImVec4 btnColor = m_presenter.GetLevelShow(i)
            ? kLevels[i].color
            : ImVec4{0.25f, 0.25f, 0.25f, 1.f};
        ImGui::PushStyleColor(ImGuiCol_Button, btnColor);
        if (ImGui::Button(kLevels[i].label))
            m_presenter.ToggleLevel(i);
        ImGui::PopStyleColor();
    }

    ImGui::Separator();

    // ── Message list ──────────────────────────────────────────────────────────
    ImGui::BeginChild("##log_scroll", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    for (const auto& e : entries) {
        const int lvl = static_cast<int>(e.level);
        if (lvl < 0 || lvl > 5 || !m_presenter.GetLevelShow(lvl))
            continue;
        ImGui::PushStyleColor(ImGuiCol_Text, kLevels[lvl].color);
        char line[2048];
        std::snprintf(line, sizeof(line), "%s [%s] %s",
                      e.timeStr.c_str(), kLevels[lvl].label, e.message.c_str());
        ImGui::TextUnformatted(line);
        ImGui::PopStyleColor();
    }

    if (gotNew)
        ImGui::SetScrollHereY(1.0f);

    ImGui::EndChild();
    ImGui::EndTabItem();
}

} // namespace StellarAlia::Editor
