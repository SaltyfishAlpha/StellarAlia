#pragma once

#include "ui/IEditorWindow.hpp"
#include "ui/presenters/ConsolePanelPresenter.hpp"
#include "EditorDiagnostics.hpp"
#include "EditorContext.hpp"

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// ConsolePanel — two-tab console (View only; all state lives in presenter).
//
// "Diagnostics" tab: editor-facing events + script log entries (auto-routed).
// "Engine Logs" tab: real-time mirror of SA_LOG_* via EditorLogCapture.
// ─────────────────────────────────────────────────────────────────────────────
class ConsolePanel : public IEditorWindow {
public:
    ConsolePanel(EditorContext& ctx, ConsolePanelPresenter& presenter)
        : m_diags(ctx.diagnostics)
        , m_presenter(presenter) {}

    std::string_view GetName() const override { return "Console"; }
    void OnDraw() override;

private:
    EditorDiagnostics*     m_diags     = nullptr;
    ConsolePanelPresenter& m_presenter;

    bool   m_showErrors   = true;
    bool   m_showWarnings = true;
    bool   m_showInfo     = true;
    size_t m_lastDiagCount = 0;

    void DrawDiagnosticsTab();
    void DrawEngineLogsTab();
};

} // namespace StellarAlia::Editor
