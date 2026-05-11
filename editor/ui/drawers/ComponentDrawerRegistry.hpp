#pragma once

#include "ui/IComponentDrawer.hpp"

#include <memory>
#include <vector>

namespace StellarAlia { class Scene; }

namespace StellarAlia::Editor {

struct EditorContext;

class ComponentDrawerRegistry {
public:
    void Register(std::unique_ptr<IComponentDrawer> drawer);

    // Calls TryDraw on each registered drawer in registration order.
    void DrawAll(entt::registry& reg, entt::entity e,
                 Scene& scene, EditorContext& ctx) const;

private:
    std::vector<std::unique_ptr<IComponentDrawer>> m_drawers;
};

} // namespace StellarAlia::Editor
