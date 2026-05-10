#pragma once

#include <entt/entt.hpp>

struct ImFont;

namespace StellarAlia { class Scene; }

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// IComponentDrawer — one instance per component type, registered with the
// InspectorPanel.
//
// TryDraw() is called every frame for the selected entity.  Implementations
// should:
//   1. Call reg.try_get<C>(entity). Return false immediately if null.
//   2. Render an ImGui CollapsingHeader + fields.
//   3. Call scene.MarkDirty(entity) if any value was modified.
//
// State (e.g. cached Euler angles, open/close flags) lives as class members,
// keeping the interface clean and avoiding closure gymnastics.
// ─────────────────────────────────────────────────────────────────────────────
class IComponentDrawer {
public:
    virtual ~IComponentDrawer() = default;

    // Returns true if the component was present on the entity.
    virtual bool TryDraw(entt::registry& reg, entt::entity entity, Scene& scene) = 0;

    void SetIconFont(ImFont* f) { m_iconFont = f; }

protected:
    ImFont* m_iconFont = nullptr;
};

} // namespace StellarAlia::Editor
