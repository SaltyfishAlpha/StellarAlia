#include "ui/drawers/CameraDrawer.hpp"
#include "EditorContext.hpp"
#include "function/scene/Components.hpp"
#include "ui/drawers/DrawerHelpers.hpp"

#include <imgui.h>
#include <glm/glm.hpp>

namespace StellarAlia::Editor {

bool CameraDrawer::TryDraw(entt::registry& reg, entt::entity entity,
                            Scene& /*scene*/, EditorContext& ctx) {
    auto* cam = reg.try_get<CameraComponent>(entity);
    if (!cam) return false;
    bool open = ImGui::CollapsingHeader("Camera", HeaderFlags());
    if (RemoveButton("x##rem_cam")) { reg.remove<CameraComponent>(entity); return true; }
    if (!open) return true;
    float fovDeg = glm::degrees(cam->fovY);
    TrackedFieldEdit(&cam->fovY, ctx, "Edit FoV",
        [&fovDeg](float* p){
            if (ImGui::DragFloat("FoV Y (deg)", &fovDeg, 0.5f, 10.f, 170.f)) {
                *p = glm::radians(fovDeg);
                return true;
            }
            return false;
        });
    TrackedFieldEdit(&cam->nearPlane, ctx, "Edit Near",
        [](float* p){ return ImGui::DragFloat("Near", p, 0.001f, 0.001f, 10.f); });
    TrackedFieldEdit(&cam->farPlane, ctx, "Edit Far",
        [](float* p){ return ImGui::DragFloat("Far", p, 1.f, 1.f, 10000.f); });
    TrackedFieldEdit(&cam->priority, ctx, "Edit Priority",
        [](int* p){ return ImGui::DragInt("Priority", p, 1.f, -100, 100); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Highest priority camera is used for the primary view.");
    return true;
}

} // namespace StellarAlia::Editor
