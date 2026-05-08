#pragma once

#include "ui/IEditorWindow.hpp"
#include "EditorDiagnostics.hpp"
#include "EditorLogCapture.hpp"
#include <vector>
#include <memory>

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// ConsolePanel — two-tab console.
//
// "Diagnostics" tab: editor-facing events that require user action
//   (shader cook failures, material errors, scene errors …).
//
// "Engine Logs" tab: real-time mirror of SA_LOG_* via EditorLogSink.
//   Only available when an EditorLogCapture is passed to the constructor.
// ─────────────────────────────────────────────────────────────────────────────
class ConsolePanel : public IEditorWindow {
public:
    explicit ConsolePanel(EditorDiagnostics& diags,
                          std::shared_ptr<EditorLogSink> logSink = nullptr)
        : m_diags(&diags), m_logSink(std::move(logSink)) {}

    std::string_view GetName() const override { return "Console"; }
    void OnDraw() override;

private:
    // ── Diagnostics tab ───────────────────────────────────────────────────────
    EditorDiagnostics* m_diags       = nullptr;
    bool m_showErrors                = true;
    bool m_showWarnings              = true;
    bool m_showInfo                  = true;
    size_t m_lastCount               = 0;

    // ── Engine Logs tab ───────────────────────────────────────────────────────
    std::shared_ptr<EditorLogSink> m_logSink;
    std::vector<LogEntry>          m_logEntries;
    int                            m_logUnread      = 0;
    float                          m_logDrainTimer  = 0.f;  // drain once per second
    // Indexed by spdlog::level::level_enum (0=trace … 5=critical)
    bool m_logLevelShow[6] = { false, false, true, true, true, true };
    static constexpr size_t  kMaxLogEntries  = 2000;
    static constexpr float   kDrainInterval  = 1.f;

    void DrawDiagnosticsTab();
    void DrawEngineLogsTab();
};

} // namespace StellarAlia::Editor
