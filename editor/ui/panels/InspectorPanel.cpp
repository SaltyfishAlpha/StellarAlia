#include "ui/panels/InspectorPanel.hpp"

#include "function/scene/Scene.hpp"
#include "function/scene/Components.hpp"

#include <imgui.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>

namespace StellarAlia::Editor {

void InspectorPanel::OnDraw() {
    uint32_t sel = m_hierarchy->GetSelectedEntity();
    if (sel == ~0u) {
        ImGui::TextDisabled("No entity selected");
        return;
    }

    auto& reg    = m_scene->Registry();
    auto  entity = static_cast<entt::entity>(sel);

    if (!reg.valid(entity)) {
        ImGui::TextDisabled("(invalid entity)");
        return;
    }

    // ── Tag ───────────────────────────────────────────────────────────────────
    if (auto* tag = reg.try_get<TagComponent>(entity)) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s", tag->name.c_str());
        if (ImGui::InputText("Name", buf, sizeof(buf)))
            tag->name = buf;
        ImGui::Separator();
    }

    // ── Transform ─────────────────────────────────────────────────────────────
    if (auto* tr = reg.try_get<TransformComponent>(entity)) {
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool changed = false;
            changed |= ImGui::DragFloat3("Position", glm::value_ptr(tr->position), 0.1f);

            // Convert quat to Euler (degrees) for editing, apply back on change.
            glm::vec3 euler = glm::degrees(glm::eulerAngles(tr->rotation));
            if (ImGui::DragFloat3("Rotation (deg)", glm::value_ptr(euler), 0.5f)) {
                tr->rotation = glm::quat(glm::radians(euler));
                changed = true;
            }

            changed |= ImGui::DragFloat3("Scale", glm::value_ptr(tr->scale), 0.01f, 0.001f, 1000.f);
            if (changed) m_scene->MarkDirty(entity);
        }
    }

    // ── Camera ────────────────────────────────────────────────────────────────
    if (auto* cam = reg.try_get<CameraComponent>(entity)) {
        if (ImGui::CollapsingHeader("Camera")) {
            float fovDeg = glm::degrees(cam->fovY);
            if (ImGui::DragFloat("FoV Y (deg)", &fovDeg, 0.5f, 10.f, 170.f))
                cam->fovY = glm::radians(fovDeg);
            ImGui::DragFloat("Near", &cam->nearPlane, 0.001f, 0.001f, 10.f);
            ImGui::DragFloat("Far",  &cam->farPlane,  1.f,    1.f,    10000.f);
        }
    }

    // ── Directional light ─────────────────────────────────────────────────────
    if (auto* dl = reg.try_get<DirectionalLightComponent>(entity)) {
        if (ImGui::CollapsingHeader("Directional Light")) {
            ImGui::ColorEdit3("Color",     glm::value_ptr(dl->color));
            ImGui::DragFloat("Intensity", &dl->intensity, 0.05f, 0.f, 100.f);
        }
    }

    // ── Point light ───────────────────────────────────────────────────────────
    if (auto* pl = reg.try_get<PointLightComponent>(entity)) {
        if (ImGui::CollapsingHeader("Point Light")) {
            ImGui::ColorEdit3("Color",     glm::value_ptr(pl->color));
            ImGui::DragFloat("Intensity", &pl->intensity, 0.05f, 0.f, 1000.f);
            ImGui::DragFloat("Range",     &pl->range,     0.1f,  0.f, 500.f);
        }
    }
}

} // namespace StellarAlia::Editor
