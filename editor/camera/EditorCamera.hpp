#pragma once

#include "function/input/InputSystem.hpp"
#include "function/renderer/CameraData.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// EditorCamera — first-person viewport camera for the editor.
//
// Lifetime and ownership:
//   Member of EditorMode. NOT a Scene entity. NOT serialized to .sascene.
//   Editor config (position, yaw, pitch) is persisted separately if needed.
//
// Renderer interface:
//   Call GetCameraData(aspect) each frame to get a CameraData suitable for
//   SceneRenderer::RenderFrame(scene, camera, w, h).
//
// Expected InputSystem action names (defined in EditorInputMaps.hpp):
//   "Look"   (Axis2D) — yaw/pitch delta (mouse delta or right stick)
//   "Move"   (Axis2D) — WASD or left stick: X=strafe, Y=forward
//   "Sprint" (Button) — speed multiplier
// ─────────────────────────────────────────────────────────────────────────────
class EditorCamera {
public:
    // ── Transform state ───────────────────────────────────────────────────────
    glm::vec3 position = { 0.f, 1.f, 0.f };
    float     yaw      = 0.f;   // degrees, Y-axis (left/right)
    float     pitch    = 0.f;   // degrees, X-axis (up/down), clamped ±80°

    // ── Projection settings ───────────────────────────────────────────────────
    float fovY      = glm::radians(70.f);  // vertical field of view, radians
    float nearPlane = 0.01f;
    float farPlane  = 1000.f;

    // ── Movement speeds (m/s) ─────────────────────────────────────────────────
    float moveSpeed   = 3.f;
    float sprintSpeed = 8.f;

    // Returns the current orientation quaternion.
    [[nodiscard]] glm::quat Rotation() const;

    // Update camera from input.
    //   mouseLook — only rotate the view when true (right mouse button held).
    void Update(const InputSystem& input, float dt, bool mouseLook);

    // Reposition along the current forward vector so 'target' appears centred
    // at 'distance' units. Yaw and pitch are unchanged.
    void FocusOn(const glm::vec3& target, float distance = 5.f);

    // Build CameraData for the renderer.
    //   aspectRatio — viewport width / height.
    [[nodiscard]] CameraData GetCameraData(float aspectRatio) const;
};

} // namespace StellarAlia::Editor
