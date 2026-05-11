#pragma once
#include "ui/IComponentDrawer.hpp"

namespace StellarAlia::Editor {

class RigidBodyDrawer : public IComponentDrawer {
public:
    bool TryDraw(entt::registry& reg, entt::entity entity,
                 Scene& scene, EditorContext& ctx) override;
};

} // namespace StellarAlia::Editor
