#pragma once

#include <functional>
#include <string>

#include "function/material/MaterialType.hpp"   // ParamDef, RHI::ParamUIType
#include "function/scene/Components.hpp"         // ParamValue

namespace StellarAlia { class MaterialManager; }

namespace StellarAlia::Editor {

struct EditorContext;

// ─────────────────────────────────────────────────────────────────────────────
// Reflected-parameter widgets — shared by MaterialOverrideDrawer and
// PostProcessPanel (Issue #88). The "data schema" is a reflected ParamDef
// (uiType / min / max / size, produced by ShaderReflectTool); these draw the
// matching ImGui control, so neither call site hardcodes field names or ranges.
// ─────────────────────────────────────────────────────────────────────────────

// A ParamValue initialised from a ParamDef's default, with the variant
// alternative chosen by the param's byte size (16→vec4, 12→vec3, 8→vec2, else float).
[[nodiscard]] ParamValue DefaultParamValue(const ParamDef& def);

// Look up a param/texture def by name across every registered MaterialType —
// used to label override entries whose owning type isn't known. Issue #103:
// promoted from MaterialOverrideDrawer so SlotOverrideEditor shares them.
[[nodiscard]] const ParamDef*   FindParamDef  (const std::string& name, const MaterialManager* matMgr);
[[nodiscard]] const TextureDef* FindTextureDef(const std::string& name, const MaterialManager* matMgr);

// Draw one reflected parameter as the widget its schema implies, editing `value`
// in place (dispatch on the value's active alternative — the caller seeds it to
// match `def`; use DefaultParamValue). `widgetLabel` is the ImGui label ("##id"
// to hide it and lay the label out manually).
//
// When `ctx != nullptr`, the edit is recorded as a single Undo command via
// TrackedFieldEdit (using `undoDesc` + `onApplied`); otherwise it's a plain edit.
// Returns true if the value changed this frame.
bool DrawReflectedParam(const ParamDef& def, ParamValue& value,
                        const char* widgetLabel,
                        EditorContext* ctx = nullptr,
                        const std::string& undoDesc = {},
                        const std::function<void()>& onApplied = {});

} // namespace StellarAlia::Editor
