#pragma once

#include "ui/IEditorWindow.hpp"
#include "EditorDiagnostics.hpp"

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// ConsolePanel — persistent editor diagnostic log.
//
// Displays all items pushed to EditorDiagnostics: shader cook failures,
// material load errors, scene errors, and (future) script compile errors.
// Colour-coded by severity; filterable by level.
//
// Does NOT duplicate the engine log stream — only editor-facing events that
// require user action are surfaced here.  Background info stays in SA_LOG_*.
// ─────────────────────────────────────────────────────────────────────────────
class ConsolePanel : public IEditorWindow {
public:
    explicit ConsolePanel(EditorDiagnostics& diags) : m_diags(&diags) {}

    std::string_view GetName() const override { return "Console"; }
    void OnDraw() override;

private:
    EditorDiagnostics* m_diags = nullptr;

    // Per-panel filter toggles.
    bool m_showErrors   = true;
    bool m_showWarnings = true;
    bool m_showInfo     = true;

    // Tracks the previous item count so new arrivals trigger auto-scroll.
    size_t m_lastCount = 0;
};

} // namespace StellarAlia::Editor
