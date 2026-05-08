#pragma once

#include "ui/IEditorWindow.hpp"
#include "EditorDiagnostics.hpp"

namespace StellarAlia { class Application; }

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// PlaybackPanel — Play / Pause / Stop toolbar for the editor.
//
// Drives Application::SetPlayState().  Play is blocked when the scene has no
// camera OR when EditorDiagnostics reports unresolved errors (e.g. a shader
// that failed to cook).  This prevents a broken pipeline from crashing the
// engine at runtime.
// ─────────────────────────────────────────────────────────────────────────────
class PlaybackPanel : public IEditorWindow {
public:
    PlaybackPanel(Application& app, EditorDiagnostics* diags)
        : m_app(&app), m_diags(diags) {}

    std::string_view GetName() const override { return "Playback"; }
    void OnDraw() override;

private:
    Application*       m_app   = nullptr;
    EditorDiagnostics* m_diags = nullptr;
};

} // namespace StellarAlia::Editor
