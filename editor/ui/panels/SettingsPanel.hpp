#pragma once

#include "ui/IEditorWindow.hpp"
#include "EditorOverlaySettings.hpp"
#include "function/physics/PhysicsSystem.hpp"
#include "function/render_graph/RenderGraph.hpp"

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// SettingsPanel — runtime editor settings (UI scale, overlay toggles, etc.)
// ─────────────────────────────────────────────────────────────────────────────
class SettingsPanel : public IEditorWindow {
public:
    explicit SettingsPanel(EditorOverlaySettings*  overlaySettings  = nullptr,
                           PhysicsDebugSettings*   physicsSettings  = nullptr,
                           const RenderGraph*      renderGraph      = nullptr)
        : m_overlaySettings(overlaySettings)
        , m_physicsSettings(physicsSettings)
        , m_renderGraph(renderGraph) {}

    std::string_view GetName() const override { return "Settings"; }
    void OnDraw() override;

private:
    EditorOverlaySettings* m_overlaySettings = nullptr;
    PhysicsDebugSettings*  m_physicsSettings  = nullptr;
    const RenderGraph*     m_renderGraph      = nullptr;
};

} // namespace StellarAlia::Editor
