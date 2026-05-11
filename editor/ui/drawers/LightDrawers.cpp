#include "ui/drawers/LightDrawers.hpp"
#include "EditorContext.hpp"
#include "function/scene/Components.hpp"
#include "ui/drawers/DrawerHelpers.hpp"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace StellarAlia::Editor {

bool DirectionalLightDrawer::TryDraw(entt::registry& reg, entt::entity entity,
                                      Scene& /*scene*/, EditorContext& /*ctx*/) {
    auto* dl = reg.try_get<DirectionalLightComponent>(entity);
    if (!dl) return false;
    bool open = ImGui::CollapsingHeader("Directional Light", HeaderFlags());
    if (RemoveButton("x##rem_dl")) { reg.remove<DirectionalLightComponent>(entity); return true; }
    if (!open) return true;
    ImGui::ColorEdit3("Color",      glm::value_ptr(dl->color));
    ImGui::DragFloat("Intensity",  &dl->intensity, 0.05f, 0.f, 100.f);
    ImGui::Checkbox("Cast Shadow", &dl->castShadow);
    return true;
}

bool PointLightDrawer::TryDraw(entt::registry& reg, entt::entity entity,
                                Scene& /*scene*/, EditorContext& /*ctx*/) {
    auto* pl = reg.try_get<PointLightComponent>(entity);
    if (!pl) return false;
    bool open = ImGui::CollapsingHeader("Point Light", HeaderFlags());
    if (RemoveButton("x##rem_pl")) { reg.remove<PointLightComponent>(entity); return true; }
    if (!open) return true;
    ImGui::ColorEdit3("Color",     glm::value_ptr(pl->color));
    ImGui::DragFloat("Intensity", &pl->intensity, 0.05f, 0.f, 1000.f);
    ImGui::DragFloat("Range",     &pl->range,     0.1f,  0.f,  500.f);
    return true;
}

bool SpotLightDrawer::TryDraw(entt::registry& reg, entt::entity entity,
                               Scene& /*scene*/, EditorContext& /*ctx*/) {
    auto* sl = reg.try_get<SpotLightComponent>(entity);
    if (!sl) return false;
    bool open = ImGui::CollapsingHeader("Spot Light", HeaderFlags());
    if (RemoveButton("x##rem_sl")) { reg.remove<SpotLightComponent>(entity); return true; }
    if (!open) return true;
    ImGui::ColorEdit3("Color",     glm::value_ptr(sl->color));
    ImGui::DragFloat("Intensity", &sl->intensity, 0.05f,  0.f, 1000.f);
    ImGui::DragFloat("Range",     &sl->range,     0.1f,   0.f,  500.f);
    float innerDeg = glm::degrees(sl->innerAngle);
    float outerDeg = glm::degrees(sl->outerAngle);
    if (ImGui::DragFloat("Inner Angle (deg)", &innerDeg, 0.5f, 0.f, 89.f))
        sl->innerAngle = glm::radians(innerDeg);
    if (ImGui::DragFloat("Outer Angle (deg)", &outerDeg, 0.5f, 0.f, 89.f))
        sl->outerAngle = glm::radians(outerDeg);
    return true;
}

bool AreaLightDrawer::TryDraw(entt::registry& reg, entt::entity entity,
                               Scene& /*scene*/, EditorContext& /*ctx*/) {
    auto* al = reg.try_get<AreaLightComponent>(entity);
    if (!al) return false;
    bool open = ImGui::CollapsingHeader("Area Light", HeaderFlags());
    if (RemoveButton("x##rem_al")) { reg.remove<AreaLightComponent>(entity); return true; }
    if (!open) return true;
    ImGui::ColorEdit3("Color",         glm::value_ptr(al->color));
    ImGui::DragFloat("Intensity",      &al->intensity,     5.f,   0.f, 10000.f);
    ImGui::DragFloat2("Size (m)",      glm::value_ptr(al->size), 0.01f, 0.01f, 100.f);
    ImGui::Checkbox("Two-Sided",       &al->twoSided);
    ImGui::DragFloat("Emissive Scale", &al->emissiveScale, 0.05f, 0.f,   10.f);
    return true;
}

} // namespace StellarAlia::Editor
