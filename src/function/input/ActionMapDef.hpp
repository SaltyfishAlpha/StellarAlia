#pragma once

#include "function/input/InputTypes.hpp"
#include <string>
#include <vector>

namespace StellarAlia {

// ─────────────────────────────────────────────────────────────────────────────
// BindingDef — definition of one physical-input → action-value mapping
//
// Create via static factory methods; chain processor calls to configure:
//
//   BindingDef::Direct("Keyboard/Space")
//   BindingDef::Direct("Gamepad/LeftStick").WithDeadZone(0.12f)
//   BindingDef::Direct("Mouse/Delta").WithScale(0.08f)
//   BindingDef::WASD()                // W=+Y S=-Y A=-X D=+X, normalised
//   BindingDef::TwoButton("Keyboard/Q", "Keyboard/E")  // → Axis [-1,1]
// ─────────────────────────────────────────────────────────────────────────────
struct BindingDef {
    enum class Kind { Direct, WASD, TwoButtonAxis, Composite };

    Kind   kind = Kind::Direct;
    std::string path;                       // Direct: device path

    struct WASDKeys {
        std::string up    = "Keyboard/W";
        std::string down  = "Keyboard/S";
        std::string left  = "Keyboard/A";
        std::string right = "Keyboard/D";
        bool normalize    = true;
    } wasd;

    struct TwoButtonKeys {
        std::string negative;
        std::string positive;
    } twoButton;

    struct CompositeKeys {
        std::vector<std::string> modifierPaths;  // all must be held; empty = no modifier
        std::string keyPath;
    } composite;

    ProcessorChain processors;

    // ── Factories ─────────────────────────────────────────────────────────────

    [[nodiscard]] static BindingDef Direct(std::string path) {
        BindingDef b;
        b.kind = Kind::Direct;
        b.path = std::move(path);
        return b;
    }

    // Standard WASD with optional custom keys.
    [[nodiscard]] static BindingDef WASD(bool normalize = true,
                                          std::string up    = "Keyboard/W",
                                          std::string down  = "Keyboard/S",
                                          std::string left  = "Keyboard/A",
                                          std::string right = "Keyboard/D") {
        BindingDef b;
        b.kind = Kind::WASD;
        b.wasd = { std::move(up), std::move(down), std::move(left), std::move(right), normalize };
        return b;
    }

    // Two buttons mapped to a single signed axis: negative → -1, positive → +1.
    [[nodiscard]] static BindingDef TwoButton(std::string negative, std::string positive) {
        BindingDef b;
        b.kind = Kind::TwoButtonAxis;
        b.twoButton = { std::move(negative), std::move(positive) };
        return b;
    }

    // Composite: all modifiers AND the key must be held to activate (AND gate).
    // Single-modifier convenience overload:
    [[nodiscard]] static BindingDef Composite(std::string modifier, std::string key) {
        BindingDef b;
        b.kind = Kind::Composite;
        b.composite = { { std::move(modifier) }, std::move(key) };
        return b;
    }
    // Multi-modifier overload (e.g. Ctrl+Shift+Z):
    [[nodiscard]] static BindingDef Composite(std::initializer_list<std::string> modifiers,
                                               std::string key) {
        BindingDef b;
        b.kind = Kind::Composite;
        b.composite = { std::vector<std::string>(modifiers), std::move(key) };
        return b;
    }
    [[nodiscard]] static BindingDef Composite(std::vector<std::string> modifiers,
                                               std::string key) {
        BindingDef b;
        b.kind = Kind::Composite;
        b.composite = { std::move(modifiers), std::move(key) };
        return b;
    }

    // ── Processor chain (fluent, return *this) ────────────────────────────────

    BindingDef& WithScale(float uniform)         { processors.Scale(uniform);     return *this; }
    BindingDef& WithScale(float x, float y)      { processors.Scale(x, y);        return *this; }
    BindingDef& WithDeadZone(float min)          { processors.DeadZone(min);      return *this; }
    BindingDef& WithInvert()                     { processors.Invert();           return *this; }
    BindingDef& WithClamp(float lo, float hi)    { processors.Clamp(lo, hi);      return *this; }
    BindingDef& WithNormalize()                  { processors.Normalize();        return *this; }
};

// ─────────────────────────────────────────────────────────────────────────────
// ActionDef — one semantic action with its bindings
// ─────────────────────────────────────────────────────────────────────────────
struct ActionDef {
    std::string             name;
    ActionType              type                = ActionType::Button;
    std::vector<BindingDef> bindings;
    float                   activationThreshold = 0.5f;
    bool                    userConfigurable    = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// ActionMapDef — named collection of actions (one input context)
//
// Only the top of the InputSystem map stack is evaluated each frame.
// passthrough=true lets the map below also receive input (e.g. overlay UI
// that shouldn't block camera look). Default is false (full block).
// ─────────────────────────────────────────────────────────────────────────────
struct ActionMapDef {
    std::string             name;
    std::vector<ActionDef>  actions;
    bool                    passthrough = false;
};

} // namespace StellarAlia
