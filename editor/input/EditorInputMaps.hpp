#pragma once

#include "function/input/ActionMapDef.hpp"

#include <vector>

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// EditorInputMaps — built-in action maps for the editor.
//
// Issue #71: canonical source for these maps is the .sainputmap files under
// engine/assets/editor/ (EditorGlobal, Viewport, TextInput, UI). This function
// is a hardcoded fallback when those builtin assets are missing or malformed —
// keep its contents in sync if you edit a builtin .sainputmap.
//
// Maps returned (Phase 3b split):
//
//   "EditorGlobal" (passthrough=false, pushed first at OnAttach, NOT popped on PIE)
//     SaveScene / NewScene / Undo / Redo / SelectAll / EntityDelete /
//     EntityDuplicate / EntityRename / GizmoTranslate / GizmoRotate / GizmoScale /
//     TogglePanels   — all userConfigurable=true
//
//   "Viewport" (passthrough=true so EditorGlobal stays reachable; popped on PIE)
//     Move / Look / Sprint / MouseLook / ToggleUI
//
//   "TextInput" (passthrough=false; pushed transiently while ImGui has keyboard focus)
//     empty   — hard-blocks everything below it (Viewport AND EditorGlobal),
//                so Ctrl+S in a rename field does not trigger SaveScene
//
//   "UI" (registered but not pushed in current EditorMode flow)
//     Navigate / Submit / Cancel
//
// Action names used by EditorCamera (map-qualified read from "Viewport"):
//   "Look", "Move", "Sprint"
// ─────────────────────────────────────────────────────────────────────────────
inline std::vector<ActionMapDef> MakeBuiltinEditorMaps() {
    return {
        {
            .name = "EditorGlobal",
            .actions = {
                { .name = "SaveScene",       .type = ActionType::Button,
                  .bindings = { BindingDef::Composite("Keyboard/LeftControl", "Keyboard/S") },
                  .userConfigurable = true },
                { .name = "NewScene",        .type = ActionType::Button,
                  .bindings = { BindingDef::Composite("Keyboard/LeftControl", "Keyboard/N") },
                  .userConfigurable = true },
                { .name = "Undo",            .type = ActionType::Button,
                  .bindings = { BindingDef::Composite("Keyboard/LeftControl", "Keyboard/Z") },
                  .userConfigurable = true },
                { .name = "Redo",            .type = ActionType::Button,
                  .bindings = { BindingDef::Composite("Keyboard/LeftControl", "Keyboard/Y") },
                  .userConfigurable = true },
                { .name = "SelectAll",       .type = ActionType::Button,
                  .bindings = { BindingDef::Composite("Keyboard/LeftControl", "Keyboard/A") },
                  .userConfigurable = true },
                { .name = "EntityDelete",    .type = ActionType::Button,
                  .bindings = { BindingDef::Direct("Keyboard/Delete") },
                  .userConfigurable = true },
                { .name = "EntityDuplicate", .type = ActionType::Button,
                  .bindings = { BindingDef::Composite("Keyboard/LeftControl", "Keyboard/D") },
                  .userConfigurable = true },
                { .name = "EntityRename",    .type = ActionType::Button,
                  .bindings = { BindingDef::Direct("Keyboard/F2") },
                  .userConfigurable = true },
                { .name = "TogglePanels",    .type = ActionType::Button,
                  .bindings = { BindingDef::Direct("Keyboard/F8") },
                  .userConfigurable = true },
            },
            .passthrough = false,
        },
        {
            .name = "Viewport",
            .actions = {
                { .name = "Move", .type = ActionType::Axis2D,
                  .bindings = {
                      BindingDef::WASD(),
                      BindingDef::Direct("Gamepad/LeftStick").WithDeadZone(0.12f),
                  } },
                { .name = "Look", .type = ActionType::Axis2D,
                  .bindings = {
                      BindingDef::Direct("Mouse/Delta").WithScale(0.08f),
                      BindingDef::Direct("Gamepad/RightStick")
                          .WithDeadZone(0.12f).WithScale(2.f, -2.f),
                  } },
                { .name = "Sprint", .type = ActionType::Button,
                  .bindings = {
                      BindingDef::Direct("Keyboard/LeftShift"),
                      BindingDef::Direct("Gamepad/LeftBumper"),
                  } },
                { .name = "MouseLook", .type = ActionType::Button,
                  .bindings = { BindingDef::Direct("Mouse/RightButton") } },
                { .name = "ToggleUI", .type = ActionType::Button,
                  .bindings = {
                      BindingDef::Direct("Keyboard/Escape"),
                      BindingDef::Direct("Gamepad/Start"),
                  },
                  .userConfigurable = true },
                // Gizmo-mode keys live in Viewport (popped on PIE) so PIE-only
                // S/T/R use by the player doesn't shove the gizmo mode and
                // doesn't conflict with WASD's Keyboard/S.
                { .name = "GizmoTranslate", .type = ActionType::Button,
                  .bindings = { BindingDef::Direct("Keyboard/T") },
                  .userConfigurable = true },
                { .name = "GizmoRotate",    .type = ActionType::Button,
                  .bindings = { BindingDef::Direct("Keyboard/R") },
                  .userConfigurable = true },
                { .name = "GizmoScale",     .type = ActionType::Button,
                  .bindings = { BindingDef::Direct("Keyboard/S") },
                  .userConfigurable = true },
            },
            .passthrough = true,   // critical: keeps EditorGlobal evaluated under Viewport
        },
        {
            // Pushed while ImGui has keyboard focus. Empty actions + passthrough=false
            // hard-blocks Viewport AND EditorGlobal beneath it.
            .name = "TextInput",
            .actions = {},
            .passthrough = false,
        },
        {
            .name = "UI",
            .actions = {
                { .name = "Navigate", .type = ActionType::Axis2D,
                  .bindings = {
                      BindingDef::WASD(/*normalize=*/false,
                                       "Keyboard/Up", "Keyboard/Down",
                                       "Keyboard/Left", "Keyboard/Right"),
                      BindingDef::Direct("Gamepad/LeftStick").WithDeadZone(0.3f),
                      BindingDef::Direct("Gamepad/DPad"),
                  } },
                { .name = "Submit", .type = ActionType::Button,
                  .bindings = {
                      BindingDef::Direct("Keyboard/Return"),
                      BindingDef::Direct("Gamepad/ButtonSouth"),
                  } },
                { .name = "Cancel", .type = ActionType::Button,
                  .bindings = {
                      BindingDef::Direct("Keyboard/Escape"),
                      BindingDef::Direct("Gamepad/ButtonEast"),
                  } },
            },
            .passthrough = false,
        },
    };
}

// Legacy alias — keep existing call sites compiling during transition.
// New code should call MakeBuiltinEditorMaps() directly.
inline std::vector<ActionMapDef> MakeViewportMaps() { return MakeBuiltinEditorMaps(); }

} // namespace StellarAlia::Editor
