#pragma once

#include "ui/IEditorWindow.hpp"
#include "EditorOverlaySettings.hpp"
#include "function/physics/PhysicsSystem.hpp"

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// SettingsPanel — runtime editor settings (UI scale, overlay toggles, etc.)
// Performance statistics live in PerformancePanel.
// ─────────────────────────────────────────────────────────────────────────────
class SettingsPanel : public IEditorWindow {
public:
    explicit SettingsPanel(EditorOverlaySettings* overlaySettings = nullptr,
                           PhysicsDebugSettings*  physicsSettings = nullptr)
        : m_overlaySettings(overlaySettings)
        , m_physicsSettings(physicsSettings) {}

    std::string_view GetName() const override { return "Settings"; }
    void OnDraw() override;

private:
    EditorOverlaySettings* m_overlaySettings = nullptr;
    PhysicsDebugSettings*  m_physicsSettings  = nullptr;
};

} // namespace StellarAlia::Editor
