#pragma once

#include <entt/entt.hpp>

namespace StellarAlia { class Scene; }
namespace StellarAlia::Editor { struct EditorContext; }

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// IComponentDrawer — one instance per component type, owned by
// ComponentDrawerRegistry.
//
// TryDraw() is called every frame for the selected entity via
// ComponentDrawerRegistry::DrawAll().  Implementations should:
//   1. Call reg.try_get<C>(entity). Return false immediately if null.
//   2. Render an ImGui CollapsingHeader + fields.
//   3. Call scene.MarkDirty(entity) if any value was modified.
//
// Per-drawer state (cached Euler angles, open flags, etc.) lives as class
// members.  Engine resources are accessed via the EditorContext& parameter.
// ─────────────────────────────────────────────────────────────────────────────
class IComponentDrawer {
public:
    virtual ~IComponentDrawer() = default;

    // Returns true if the component was present on the entity.
    virtual bool TryDraw(entt::registry& reg, entt::entity entity,
                         Scene& scene, EditorContext& ctx) = 0;
};

} // namespace StellarAlia::Editor
