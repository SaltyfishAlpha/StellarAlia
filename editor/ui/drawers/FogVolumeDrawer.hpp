#pragma once
#include "ui/IComponentDrawer.hpp"

namespace StellarAlia::Editor {

// Issue #110: local volumetric fog volume (unit box × entity transform).
class FogVolumeDrawer : public IComponentDrawer {
public:
    bool TryDraw(entt::registry& reg, entt::entity entity,
                 Scene& scene, EditorContext& ctx) override;
};

} // namespace StellarAlia::Editor
