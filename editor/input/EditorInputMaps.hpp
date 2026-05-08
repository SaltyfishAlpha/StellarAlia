#pragma once

#include "function/input/ActionMapDef.hpp"

#include <vector>

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// EditorInputMaps — built-in action maps for the editor viewport.
//
// Maps returned:
//
//   "Viewport" (default active map)
//     Move       Axis2D   WASD / Gamepad LeftStick
//     Look       Axis2D   Mouse/Delta (RMB-gated) / Gamepad RightStick
//     Sprint     Button   LeftShift / Gamepad LeftBumper
//     MouseLook  Button   Mouse/RightButton  (gate for keyboard+mouse look)
//     ToggleUI   Button   Escape / Gamepad Start
//
//   "UI"
//     Navigate   Axis2D   Arrow keys / Gamepad LeftStick / DPad
//     Submit     Button   Return / Gamepad ButtonSouth
//     Cancel     Button   Escape / Gamepad ButtonEast
//
// Action names used by EditorCamera::Update:
//   "Look", "Move", "Sprint"
// ─────────────────────────────────────────────────────────────────────────────
inline std::vector<ActionMapDef> MakeViewportMaps() {
    return {
        {
            .name = "Viewport",
            .actions = {
                {
                    .name = "Move",
                    .type = ActionType::Axis2D,
                    .bindings = {
                        BindingDef::WASD(),
                        BindingDef::Direct("Gamepad/LeftStick").WithDeadZone(0.12f),
                    }
                },
                {
                    .name = "Look",
                    .type = ActionType::Axis2D,
                    .bindings = {
                        BindingDef::Direct("Mouse/Delta").WithScale(0.08f),
                        BindingDef::Direct("Gamepad/RightStick")
                            .WithDeadZone(0.12f).WithScale(2.f, -2.f),
                    }
                },
                {
                    .name = "Sprint",
                    .type = ActionType::Button,
                    .bindings = {
                        BindingDef::Direct("Keyboard/LeftShift"),
                        BindingDef::Direct("Gamepad/LeftBumper"),
                    }
                },
                {
                    .name = "MouseLook",
                    .type = ActionType::Button,
                    .bindings = {
                        BindingDef::Direct("Mouse/RightButton"),
                    }
                },
                {
                    .name = "ToggleUI",
                    .type = ActionType::Button,
                    .bindings = {
                        BindingDef::Direct("Keyboard/Escape"),
                        BindingDef::Direct("Gamepad/Start"),
                    },
                    .userConfigurable = true,
                },
                // ── Gizmo mode shortcuts ──────────────────────────────────
                {
                    .name = "GizmoTranslate",
                    .type = ActionType::Button,
                    .bindings = { BindingDef::Direct("Keyboard/T") },
                    .userConfigurable = true,
                },
                {
                    .name = "GizmoRotate",
                    .type = ActionType::Button,
                    .bindings = { BindingDef::Direct("Keyboard/R") },
                    .userConfigurable = true,
                },
                {
                    .name = "GizmoScale",
                    .type = ActionType::Button,
                    .bindings = { BindingDef::Direct("Keyboard/S") },
                    .userConfigurable = true,
                },
                // ── Selection shortcuts ───────────────────────────────────
                {
                    .name = "SelectAll",
                    .type = ActionType::Button,
                    .bindings = { BindingDef::Composite("Keyboard/LeftControl", "Keyboard/A") },
                    .userConfigurable = true,
                },
                // ── File shortcuts ────────────────────────────────────────
                {
                    .name = "NewScene",
                    .type = ActionType::Button,
                    .bindings = { BindingDef::Composite("Keyboard/LeftControl", "Keyboard/N") },
                    .userConfigurable = true,
                },
                {
                    .name = "SaveScene",
                    .type = ActionType::Button,
                    .bindings = { BindingDef::Composite("Keyboard/LeftControl", "Keyboard/S") },
                    .userConfigurable = true,
                },
                // ── Scene Hierarchy shortcuts ─────────────────────────────
                {
                    .name = "EntityDelete",
                    .type = ActionType::Button,
                    .bindings = { BindingDef::Direct("Keyboard/Delete") },
                    .userConfigurable = true,
                },
                {
                    .name = "EntityDuplicate",
                    .type = ActionType::Button,
                    .bindings = { BindingDef::Composite("Keyboard/LeftControl", "Keyboard/D") },
                    .userConfigurable = true,
                },
                {
                    .name = "EntityRename",
                    .type = ActionType::Button,
                    .bindings = { BindingDef::Direct("Keyboard/F2") },
                    .userConfigurable = true,
                },
            }
        },
        {
            // Pushed while ImGui has keyboard focus (text input fields, rename, etc.).
            // Empty actions + passthrough=false blocks all key/gamepad actions below.
            .name = "TextInput",
            .actions = {},
            .passthrough = false,
        },
        {
            .name = "UI",
            .actions = {
                {
                    .name = "Navigate",
                    .type = ActionType::Axis2D,
                    .bindings = {
                        BindingDef::WASD(/*normalize=*/false,
                                         "Keyboard/Up", "Keyboard/Down",
                                         "Keyboard/Left", "Keyboard/Right"),
                        BindingDef::Direct("Gamepad/LeftStick").WithDeadZone(0.3f),
                        BindingDef::Direct("Gamepad/DPad"),
                    }
                },
                {
                    .name = "Submit",
                    .type = ActionType::Button,
                    .bindings = {
                        BindingDef::Direct("Keyboard/Return"),
                        BindingDef::Direct("Gamepad/ButtonSouth"),
                    }
                },
                {
                    .name = "Cancel",
                    .type = ActionType::Button,
                    .bindings = {
                        BindingDef::Direct("Keyboard/Escape"),
                        BindingDef::Direct("Gamepad/ButtonEast"),
                    }
                },
            }
        },
    };
}

} // namespace StellarAlia::Editor
