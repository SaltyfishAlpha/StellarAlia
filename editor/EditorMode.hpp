#pragma once

#include "engine/AppMode.hpp"
#include "camera/EditorCamera.hpp"
#include "ui/EditorUI.hpp"

namespace StellarAlia { class Application; }

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// EditorMode — the engine's authoring mode.
//
// Owns the editor viewport camera. Registers the Viewport / UI action maps
// and drives the camera from input each frame.
//
// Lifetime: created by main(), handed to Application, alive until Shutdown().
// ─────────────────────────────────────────────────────────────────────────────
class EditorMode final : public AppMode {
public:
    void OnAttach(Application& app) override;
    void OnDetach() override;
    void OnUpdate(float dt) override;
    void OnRenderUI(RHI::IRHICommandList* cmd) override;
    [[nodiscard]] CameraData GetCameraData(float aspectRatio) const override;

private:
    Application* m_app = nullptr;
    EditorCamera m_camera;
    EditorUI     m_ui;
};

} // namespace StellarAlia::Editor
