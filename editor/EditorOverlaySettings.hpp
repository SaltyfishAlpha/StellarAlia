#pragma once

#include <cstdint>

namespace StellarAlia::Editor {

enum class GizmoMode : uint8_t { Translate, Rotate, Scale };

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

    // ── Light wireframes ──────────────────────────────────────────────────────
    bool drawPointLightRange     = true;
    bool drawSpotLightCone       = true;
    bool drawAreaLightRect       = true;
    bool drawDirectionalLightDir = true;

    // ── Selection collider ───────────────────────────────────────────────────
    bool drawSelectionCollider = true;  // draw collider wireframe on selected entity

    // ── Skeleton gizmo ───────────────────────────────────────────────────────
    bool drawSkeletonGizmo = true;  // joints (spheres) + bones (lines) on selected skinned mesh

    // ── Entity icons (lights, cameras) ───────────────────────────────────────
    bool  drawEntityIcons   = true;   // billboard icons for non-visible entities
    float billboardIconSize = 32.f;   // rendered size in screen pixels [16, 64]

    // ── Selection outline ─────────────────────────────────────────────────────
    bool  drawSelectionAABB = true;   // enables screen-space silhouette outline
    float outlineWidth      = 2.f;    // dilation radius in pixels [1, 8]

    // ── Gizmo ────────────────────────────────────────────────────────────────
    bool      drawGizmo      = true;
    GizmoMode gizmoMode      = GizmoMode::Translate;  // T / R / S to cycle
    bool      gizmoWorldSpace = true;                 // world vs local space
};

} // namespace StellarAlia::Editor
