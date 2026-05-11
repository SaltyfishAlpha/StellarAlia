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
                              Scene& /*scene*/, EditorContext& /*ctx*/) {
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
            ImGui::DragFloat3("Half Extents", glm::value_ptr(col->extents),
                              0.01f, 0.001f, 100.f);
            break;
        case ColliderComponent::Shape::Sphere:
            ImGui::DragFloat("Radius", &col->extents.x, 0.01f, 0.001f, 100.f);
            break;
        case ColliderComponent::Shape::Capsule:
            ImGui::DragFloat("Radius",      &col->extents.x, 0.01f, 0.001f, 100.f);
            ImGui::DragFloat("Half Height", &col->extents.y, 0.01f, 0.001f, 100.f);
            break;
    }

    ImGui::Separator();
    ImGui::DragFloat3("Center Offset", glm::value_ptr(col->offset), 0.01f);

    // Euler edit for shape orientation (applied on body creation / restart).
    glm::vec3 euler = glm::degrees(glm::eulerAngles(col->rotation));
    if (ImGui::DragFloat3("Orientation (deg)", glm::value_ptr(euler), 0.5f))
        col->rotation = glm::quat(glm::radians(euler));

    return true;
}

} // namespace StellarAlia::Editor
