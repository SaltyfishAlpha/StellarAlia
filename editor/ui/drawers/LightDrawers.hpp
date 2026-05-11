#pragma once
#include "ui/IComponentDrawer.hpp"

namespace StellarAlia::Editor {

class DirectionalLightDrawer : public IComponentDrawer {
public:
    bool TryDraw(entt::registry& reg, entt::entity entity,
                 Scene& scene, EditorContext& ctx) override;
};

class PointLightDrawer : public IComponentDrawer {
public:
    bool TryDraw(entt::registry& reg, entt::entity entity,
                 Scene& scene, EditorContext& ctx) override;
};

class SpotLightDrawer : public IComponentDrawer {
public:
    bool TryDraw(entt::registry& reg, entt::entity entity,
                 Scene& scene, EditorContext& ctx) override;
};

class AreaLightDrawer : public IComponentDrawer {
public:
    bool TryDraw(entt::registry& reg, entt::entity entity,
                 Scene& scene, EditorContext& ctx) override;
};

} // namespace StellarAlia::Editor
