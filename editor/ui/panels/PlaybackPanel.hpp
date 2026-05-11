#pragma once

#include "ui/IEditorWindow.hpp"
#include "ui/presenters/PlaybackPresenter.hpp"
#include "EditorDiagnostics.hpp"
#include "EditorContext.hpp"

namespace StellarAlia { class Application; }

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// PlaybackPanel — Play / Pause / Stop toolbar for the editor.
//
// Drives Application::SetPlayState() via PlaybackPresenter.  Play is blocked
// when the scene has no camera OR when EditorDiagnostics reports unresolved
// errors (e.g. a shader that failed to cook).
// ─────────────────────────────────────────────────────────────────────────────
class PlaybackPanel : public IEditorWindow {
public:
    PlaybackPanel(EditorContext& ctx, PlaybackPresenter& presenter)
        : m_presenter(presenter), m_app(ctx.app), m_diags(ctx.diagnostics) {}

    std::string_view GetName() const override { return "Playback"; }
    void OnDraw() override;

private:
    PlaybackPresenter& m_presenter;
    Application*       m_app   = nullptr;
    EditorDiagnostics* m_diags = nullptr;
};

} // namespace StellarAlia::Editor
