#include "ui/drawers/ColliderDrawer.hpp"
#include "EditorContext.hpp"
#include "function/scene/Components.hpp"
#include "ui/drawers/DrawerHelpers.hpp"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>

namespace StellarAlia::Editor {

bool ColliderDrawer::TryDraw(entt::registry& reg, entt::entity entity,
                              Scene& /*scene*/, EditorContext& ctx) {
    auto* col = reg.try_get<ColliderComponent>(entity);
    if (!col) return false;
    bool open = ImGui::CollapsingHeader("Collider", HeaderFlags());
    if (RemoveButton("x##rem_col")) { reg.remove<ColliderComponent>(entity); return true; }
    if (!open) return true;

    const char* shapes[] = { "Box", "Sphere", "Capsule" };
    int s = static_cast<int>(col->shape);
    if (ImGui::Combo("Shape", &s, shapes, 3))
        col->shape = static_cast<ColliderComponent::Shape>(s);

    switch (col->shape) {
        case ColliderComponent::Shape::Box:
            TrackedFieldEdit(&col->extents, ctx, "Edit Half Extents",
                [](glm::vec3* p){
                    return ImGui::DragFloat3("Half Extents", glm::value_ptr(*p),
                                             0.01f, 0.001f, 100.f);
                });
            break;
        case ColliderComponent::Shape::Sphere:
            TrackedFieldEdit(&col->extents.x, ctx, "Edit Radius",
                [](float* p){ return ImGui::DragFloat("Radius", p, 0.01f, 0.001f, 100.f); });
            break;
        case ColliderComponent::Shape::Capsule:
            TrackedFieldEdit(&col->extents.x, ctx, "Edit Radius",
                [](float* p){ return ImGui::DragFloat("Radius", p, 0.01f, 0.001f, 100.f); });
            TrackedFieldEdit(&col->extents.y, ctx, "Edit Half Height",
                [](float* p){ return ImGui::DragFloat("Half Height", p, 0.01f, 0.001f, 100.f); });
            break;
    }

    ImGui::Separator();
    TrackedFieldEdit(&col->offset, ctx, "Edit Center Offset",
        [](glm::vec3* p){ return ImGui::DragFloat3("Center Offset", glm::value_ptr(*p), 0.01f); });

    // Euler edit for shape orientation (applied on body creation / restart).
    TrackedFieldEdit(&col->rotation, ctx, "Edit Orientation",
        [](glm::quat* q){
            glm::vec3 euler = glm::degrees(glm::eulerAngles(*q));
            if (ImGui::DragFloat3("Orientation (deg)", glm::value_ptr(euler), 0.5f)) {
                *q = glm::quat(glm::radians(euler));
                return true;
            }
            return false;
        });

    return true;
}

} // namespace StellarAlia::Editor
