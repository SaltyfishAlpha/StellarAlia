#pragma once

#include <entt/entt.hpp>
#include <cstdint>

namespace StellarAlia {
class Scene;
class MaterialType;
}

namespace StellarAlia::Editor {

struct EditorContext;

// ─────────────────────────────────────────────────────────────────────────────
// Per-slot material override editor (Issue #103).
//
// Draws the editing body for one material slot inside an already-open region
// (the expanded MeshRenderer slot row): alphaMode/doubleSided inherit-combos,
// scalar/texture override lists, and the reflected "+ Add" popup. Data lives in
// MaterialOverrideComponent::slotOverrides — the component and the slot entry
// are created on demand by the first edit (undo tears down exactly what the
// command created). All edits go through CommandManager when available.
//
// `effType` is the slot's effective MaterialType (used to enumerate ParamDef /
// TextureDef in the add popup); nullptr falls back to the union of all
// registered types. Returns true when anything changed this frame (caller
// marks the scene material-dirty).
// ─────────────────────────────────────────────────────────────────────────────
bool DrawSlotOverrideEditor(entt::registry& reg, entt::entity entity,
                            Scene& scene, EditorContext& ctx,
                            int32_t slot, const MaterialType* effType);

} // namespace StellarAlia::Editor
