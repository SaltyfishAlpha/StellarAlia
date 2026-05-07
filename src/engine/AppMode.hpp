#pragma once

#include "engine/EnginePlayState.hpp"
#include "function/renderer/CameraData.hpp"
#include "platform/rhi/IRHIDevice.hpp"

namespace StellarAlia {

class Application;

// ─────────────────────────────────────────────────────────────────────────────
// AppMode — pluggable application mode (Editor, Game, …).
//
// Application owns one AppMode at a time. The mode receives access to all core
// engine systems via Application& in OnAttach and drives the per-frame logic.
//
// Lifecycle:
//   Application::Init()  → mode->OnAttach(app)
//   Application::Run()   → loop: mode->OnUpdate(dt)
//   Application::Shutdown() → mode->OnDetach()
// ─────────────────────────────────────────────────────────────────────────────
class AppMode {
public:
    virtual ~AppMode() = default;

    // Called once after all engine systems are initialised.
    virtual void OnAttach(Application& app) = 0;

    // Called once before engine systems are destroyed.
    virtual void OnDetach() = 0;

    // Called every frame before rendering. Read input and update state here.
    virtual void OnUpdate(float dt) = 0;

    // Return the camera to use for this frame's render call.
    // aspectRatio = viewport width / height.
    [[nodiscard]] virtual CameraData GetCameraData(float aspectRatio) const = 0;

    // Called each frame after 3D rendering, before the command buffer is
    // submitted. Record ImGui draw calls into `cmd` here.
    // Default is a no-op so non-editor modes don't need to override.
    virtual void OnRenderUI(RHI::IRHICommandList* /*cmd*/) {}

    // Called immediately after the engine play state changes.
    // Override to react to Play / Pause / Stop transitions (e.g. swap input maps).
    virtual void OnPlayStateChanged(EnginePlayState /*newState*/) {}
};

} // namespace StellarAlia
