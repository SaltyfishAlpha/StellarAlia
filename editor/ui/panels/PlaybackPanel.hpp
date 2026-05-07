#pragma once

#include "ui/IEditorWindow.hpp"

namespace StellarAlia { class Application; }

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// PlaybackPanel — Play / Pause / Stop toolbar for the editor.
//
// Drives Application::SetPlayState().  Animation systems only tick while the
// engine is in the Playing state.
// ─────────────────────────────────────────────────────────────────────────────
class PlaybackPanel : public IEditorWindow {
public:
    explicit PlaybackPanel(Application& app) : m_app(&app) {}

    std::string_view GetName() const override { return "Playback"; }
    void OnDraw() override;

private:
    Application* m_app = nullptr;
};

} // namespace StellarAlia::Editor
