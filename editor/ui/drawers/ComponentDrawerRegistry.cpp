#include "ui/drawers/ComponentDrawerRegistry.hpp"
#include "EditorContext.hpp"
#include "function/scene/Scene.hpp"

namespace StellarAlia::Editor {

void ComponentDrawerRegistry::Register(std::unique_ptr<IComponentDrawer> drawer) {
    m_drawers.push_back(std::move(drawer));
}

void ComponentDrawerRegistry::DrawAll(entt::registry& reg, entt::entity e,
                                      Scene& scene, EditorContext& ctx) const {
    for (const auto& d : m_drawers)
        d->TryDraw(reg, e, scene, ctx);
}

} // namespace StellarAlia::Editor
