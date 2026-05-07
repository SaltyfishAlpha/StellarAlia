#pragma once

#include "function/debug/DebugDraw.hpp"

#include <glm/glm.hpp>

namespace StellarAlia::Editor {

enum class GizmoMode : uint8_t { Translate, Rotate, Scale };

// ─────────────────────────────────────────────────────────────────────────────
// GizmoSystem — draw-only transform gizmo for the editor overlay.
//
// Call Draw() from EditorMode::DrawOverlays() each frame with the selected
// entity's world matrix and a desired screen-space size.
//
// Translate : 3 coloured arrows (X=red, Y=green, Z=blue)
// Rotate    : 3 coloured rings  (X=red, Y=green, Z=blue)
// Scale     : 3 coloured arrows + box tips
// ─────────────────────────────────────────────────────────────────────────────
class GizmoSystem {
public:
    GizmoMode mode = GizmoMode::Translate;

    void Draw(DebugDraw& dd, const glm::mat4& world, float size = 1.f) const;

private:
    static void DrawTranslate(DebugDraw& dd, glm::vec3 origin,
                              glm::vec3 ax, glm::vec3 ay, glm::vec3 az, float size);
    static void DrawRotate   (DebugDraw& dd, glm::vec3 origin,
                              glm::vec3 ax, glm::vec3 ay, glm::vec3 az, float size);
    static void DrawScale    (DebugDraw& dd, glm::vec3 origin,
                              glm::vec3 ax, glm::vec3 ay, glm::vec3 az, float size);
};

} // namespace StellarAlia::Editor
