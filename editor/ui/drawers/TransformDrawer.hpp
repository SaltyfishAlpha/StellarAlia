#pragma once
#include "ui/IComponentDrawer.hpp"
#include <glm/vec3.hpp>
#include <cstdint>

namespace StellarAlia::Editor {

class TransformDrawer : public IComponentDrawer {
public:
    bool TryDraw(entt::registry& reg, entt::entity entity,
                 Scene& scene, EditorContext& ctx) override;

private:
    uint32_t  m_cachedEulerEntity = ~0u;
    glm::vec3 m_cachedEuler       = {};
    bool      m_scaleLocked       = true;
};

} // namespace StellarAlia::Editor
