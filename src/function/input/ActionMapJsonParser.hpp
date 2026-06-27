#pragma once

#include "function/input/ActionMapDef.hpp"

#include <string>
#include <string_view>

namespace StellarAlia {

// Two-way codec between .sainputmap JSON and ActionMapDef.
//
// JSON schema (one map per file):
//   {
//     "name": "<map name>",
//     "passthrough": false,
//     "actions": [
//       { "name": "<action>", "type": "Button|Axis|Axis2D",
//         "activationThreshold": 0.5, "userConfigurable": false,
//         "bindings": [ {...}, ... ] }
//     ]
//   }
//
// Binding kinds:
//   { "kind": "Direct", "path": "Keyboard/W", "scale": 1.0|[x,y], "deadZone": 0.12,
//     "invert": true, "normalize": true, "clamp": [lo, hi] }
//   { "kind": "WASD", "up":"…","down":"…","left":"…","right":"…", "normalize": true }
//   { "kind": "TwoButton", "negative": "…", "positive": "…" }
//   { "kind": "Composite", "modifiers": ["Keyboard/LeftControl",…], "key": "Keyboard/S" }
struct ActionMapJsonParser {
    // JSON text → ActionMapDef. Returns false on parse or structural error.
    static bool Parse(std::string_view json, ActionMapDef& out);

    // ActionMapDef → pretty-printed JSON. Always succeeds.
    static void Serialize(const ActionMapDef& def, std::string& outJson);
};

} // namespace StellarAlia
