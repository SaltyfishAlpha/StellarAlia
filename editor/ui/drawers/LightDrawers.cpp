#include "ui/drawers/LightDrawers.hpp"
#include "EditorContext.hpp"
#include "function/scene/Components.hpp"
#include "ui/drawers/DrawerHelpers.hpp"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace StellarAlia::Editor {

bool DirectionalLightDrawer::TryDraw(entt::registry& reg, entt::entity entity,
                                      Scene& /*scene*/, EditorContext& ctx) {
    auto* dl = reg.try_get<DirectionalLightComponent>(entity);
    if (!dl) return false;
    bool open = ImGui::CollapsingHeader("Directional Light", HeaderFlags());
    if (RemoveComponentButton<DirectionalLightComponent>("x##rem_dl", reg, entity, ctx, "Remove Directional Light")) return true;
    if (!open) return true;
    TrackedFieldEdit(&dl->color, ctx, "Edit Color",
        [](glm::vec3* p){ return ImGui::ColorEdit3("Color", glm::value_ptr(*p)); });
    TrackedFieldEdit(&dl->intensity, ctx, "Edit Intensity",
        [](float* p){ return ImGui::DragFloat("Intensity", p, 0.05f, 0.f, 100.f); });
    TrackedFieldEdit(&dl->castShadow, ctx, "Toggle Cast Shadow",
        [](bool* p){ return ImGui::Checkbox("Cast Shadow", p); });
    TrackedFieldEdit(&dl->isSun, ctx, "Toggle Sun Source",
        [](bool* p){ return ImGui::Checkbox("Sun Source", p); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Drives the shadow map and volumetric fog god rays.\n"
                          "First marked directional light wins; none marked = first found.");
    return true;
}

bool PointLightDrawer::TryDraw(entt::registry& reg, entt::entity entity,
                                Scene& /*scene*/, EditorContext& ctx) {
    auto* pl = reg.try_get<PointLightComponent>(entity);
    if (!pl) return false;
    bool open = ImGui::CollapsingHeader("Point Light", HeaderFlags());
    if (RemoveComponentButton<PointLightComponent>("x##rem_pl", reg, entity, ctx, "Remove Point Light")) return true;
    if (!open) return true;
    TrackedFieldEdit(&pl->color, ctx, "Edit Color",
        [](glm::vec3* p){ return ImGui::ColorEdit3("Color", glm::value_ptr(*p)); });
    TrackedFieldEdit(&pl->intensity, ctx, "Edit Intensity",
        [](float* p){ return ImGui::DragFloat("Intensity", p, 0.05f, 0.f, 1000.f); });
    TrackedFieldEdit(&pl->range, ctx, "Edit Range",
        [](float* p){ return ImGui::DragFloat("Range", p, 0.1f, 0.f, 500.f); });
    return true;
}

bool SpotLightDrawer::TryDraw(entt::registry& reg, entt::entity entity,
                               Scene& /*scene*/, EditorContext& ctx) {
    auto* sl = reg.try_get<SpotLightComponent>(entity);
    if (!sl) return false;
    bool open = ImGui::CollapsingHeader("Spot Light", HeaderFlags());
    if (RemoveComponentButton<SpotLightComponent>("x##rem_sl", reg, entity, ctx, "Remove Spot Light")) return true;
    if (!open) return true;
    TrackedFieldEdit(&sl->color, ctx, "Edit Color",
        [](glm::vec3* p){ return ImGui::ColorEdit3("Color", glm::value_ptr(*p)); });
    TrackedFieldEdit(&sl->intensity, ctx, "Edit Intensity",
        [](float* p){ return ImGui::DragFloat("Intensity", p, 0.05f, 0.f, 1000.f); });
    TrackedFieldEdit(&sl->range, ctx, "Edit Range",
        [](float* p){ return ImGui::DragFloat("Range", p, 0.1f, 0.f, 500.f); });
    float innerDeg = glm::degrees(sl->innerAngle);
    float outerDeg = glm::degrees(sl->outerAngle);
    TrackedFieldEdit(&sl->innerAngle, ctx, "Edit Inner Angle",
        [&innerDeg](float* p){
            if (ImGui::DragFloat("Inner Angle (deg)", &innerDeg, 0.5f, 0.f, 89.f)) {
                *p = glm::radians(innerDeg); return true;
            }
            return false;
        });
    TrackedFieldEdit(&sl->outerAngle, ctx, "Edit Outer Angle",
        [&outerDeg](float* p){
            if (ImGui::DragFloat("Outer Angle (deg)", &outerDeg, 0.5f, 0.f, 89.f)) {
                *p = glm::radians(outerDeg); return true;
            }
            return false;
        });
    return true;
}

bool AreaLightDrawer::TryDraw(entt::registry& reg, entt::entity entity,
                               Scene& /*scene*/, EditorContext& ctx) {
    auto* al = reg.try_get<AreaLightComponent>(entity);
    if (!al) return false;
    bool open = ImGui::CollapsingHeader("Area Light", HeaderFlags());
    if (RemoveComponentButton<AreaLightComponent>("x##rem_al", reg, entity, ctx, "Remove Area Light")) return true;
    if (!open) return true;
    TrackedFieldEdit(&al->color, ctx, "Edit Color",
        [](glm::vec3* p){ return ImGui::ColorEdit3("Color", glm::value_ptr(*p)); });
    TrackedFieldEdit(&al->intensity, ctx, "Edit Intensity",
        [](float* p){ return ImGui::DragFloat("Intensity", p, 5.f, 0.f, 10000.f); });
    TrackedFieldEdit(&al->size, ctx, "Edit Size",
        [](glm::vec2* p){ return ImGui::DragFloat2("Size (m)", glm::value_ptr(*p), 0.01f, 0.01f, 100.f); });
    TrackedFieldEdit(&al->twoSided, ctx, "Toggle Two-Sided",
        [](bool* p){ return ImGui::Checkbox("Two-Sided", p); });
    TrackedFieldEdit(&al->emissiveScale, ctx, "Edit Emissive Scale",
        [](float* p){ return ImGui::DragFloat("Emissive Scale", p, 0.05f, 0.f, 10.f); });
    return true;
}

} // namespace StellarAlia::Editor
