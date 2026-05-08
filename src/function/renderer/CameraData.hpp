#pragma once

#include <glm/glm.hpp>

namespace StellarAlia {

// ─────────────────────────────────────────────────────────────────────────────
// CameraData — device-agnostic camera descriptor passed to SceneRenderer.
//
// Both the editor camera (EditorMode member, not a Scene entity) and the
// game camera (Scene entity with CameraComponent) talk to
// the renderer through this struct.  SceneRenderer does not distinguish between
// the two sources.
//
// Conventions (must match the renderer's frame_uniforms.glsl):
//   view  — world → camera space
//   proj  — camera → clip space, Vulkan NDC (Y flipped, depth [0,1])
//   worldPosition — camera origin in world space (used for specular etc.)
//
// The caller is responsible for building correct matrices:
//   view = glm::inverse(worldTransformMatrix);
//   proj = glm::perspective(fovY, aspect, nearZ, farZ);
//   proj[1][1] *= -1.f;   // Vulkan Y-flip
// ─────────────────────────────────────────────────────────────────────────────
struct CameraData {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec3 worldPosition;
};

} // namespace StellarAlia
