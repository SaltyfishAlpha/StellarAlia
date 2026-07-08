#include "ui/drawers/FogVolumeDrawer.hpp"
#include "EditorContext.hpp"
#include "function/scene/Components.hpp"
#include "ui/drawers/DrawerHelpers.hpp"

#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>

namespace StellarAlia::Editor {

bool FogVolumeDrawer::TryDraw(entt::registry& reg, entt::entity entity,
                              Scene& /*scene*/, EditorContext& ctx) {
    auto* fv = reg.try_get<FogVolumeComponent>(entity);
    if (!fv) return false;
    bool open = ImGui::CollapsingHeader("Fog Volume", HeaderFlags());
    if (RemoveComponentButton<FogVolumeComponent>("x##rem_fv", reg, entity, ctx, "Remove Fog Volume")) return true;
    if (!open) return true;
    ImGui::TextDisabled("Box shape from entity transform (scale = extents)");
    TrackedFieldEdit(&fv->density, ctx, "Edit Fog Density",
        [](float* p){ return ImGui::DragFloat("Density", p, 0.005f, 0.f, 2.f, "%.3f"); });
    TrackedFieldEdit(&fv->albedo, ctx, "Edit Fog Albedo",
        [](glm::vec3* p){ return ImGui::ColorEdit3("Albedo", glm::value_ptr(*p)); });
    TrackedFieldEdit(&fv->falloff, ctx, "Edit Fog Falloff",
        [](float* p){ return ImGui::SliderFloat("Edge Falloff", p, 0.f, 0.5f, "%.2f"); });
    return true;
}

} // namespace StellarAlia::Editor
