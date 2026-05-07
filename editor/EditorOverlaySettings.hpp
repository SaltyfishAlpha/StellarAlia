#pragma once

#include "gizmo/GizmoSystem.hpp"

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// EditorOverlaySettings — visibility toggles for all editor overlay symbols.
//
// Owned by EditorMode. SettingsPanel holds a raw pointer for UI editing.
// enabled = false when Playing/Paused (zero draw-call overhead).
// ─────────────────────────────────────────────────────────────────────────────
struct EditorOverlaySettings {
    bool enabled           = true;   // master switch; false in Playing/Paused

    // ── Scene helpers ─────────────────────────────────────────────────────────
    bool drawGrid          = true;
    bool drawWorldAxes     = true;
    bool drawEntityAxes    = false;

    // ── Camera ────────────────────────────────────────────────────────────────
    bool drawCameraFrustum = true;

    // ── Selection collider ───────────────────────────────────────────────────
    bool drawSelectionCollider = true;  // draw collider wireframe on selected entity

    // ── Selection outline ─────────────────────────────────────────────────────
    bool  drawSelectionAABB = true;   // enables screen-space silhouette outline
    float outlineWidth      = 2.f;    // dilation radius in pixels [1, 8]

    // ── Gizmo ────────────────────────────────────────────────────────────────
    bool      drawGizmo   = true;
    GizmoMode gizmoMode   = GizmoMode::Translate;  // T / R / S to cycle
};

} // namespace StellarAlia::Editor
