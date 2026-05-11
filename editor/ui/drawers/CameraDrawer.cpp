#include "ui/drawers/CameraDrawer.hpp"
#include "EditorContext.hpp"
#include "function/scene/Components.hpp"
#include "ui/drawers/DrawerHelpers.hpp"

#include <imgui.h>
#include <glm/glm.hpp>

namespace StellarAlia::Editor {

bool CameraDrawer::TryDraw(entt::registry& reg, entt::entity entity,
                            Scene& /*scene*/, EditorContext& /*ctx*/) {
    auto* cam = reg.try_get<CameraComponent>(entity);
    if (!cam) return false;
    bool open = ImGui::CollapsingHeader("Camera", HeaderFlags());
    if (RemoveButton("x##rem_cam")) { reg.remove<CameraComponent>(entity); return true; }
    if (!open) return true;
    float fovDeg = glm::degrees(cam->fovY);
    if (ImGui::DragFloat("FoV Y (deg)", &fovDeg, 0.5f, 10.f, 170.f))
        cam->fovY = glm::radians(fovDeg);
    ImGui::DragFloat("Near",     &cam->nearPlane, 0.001f, 0.001f,  10.f);
    ImGui::DragFloat("Far",      &cam->farPlane,  1.f,    1.f,    10000.f);
    ImGui::DragInt  ("Priority", &cam->priority,  1.f,   -100,    100);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Highest priority camera is used for the primary view.");
    return true;
}

} // namespace StellarAlia::Editor
