#include "ui/panels/InspectorPanel.hpp"

#include "ui/ComponentDrawers.hpp"
#include "function/scene/Scene.hpp"
#include "function/scene/Components.hpp"

#include <imgui.h>
#include <entt/entt.hpp>

namespace StellarAlia::Editor {

InspectorPanel::InspectorPanel(Scene& scene, const SceneHierarchyPanel& hierarchy)
    : m_scene(&scene), m_hierarchy(&hierarchy)
{
    RegisterDrawers();
}

void InspectorPanel::RegisterDrawers() {
    m_drawers.push_back(std::make_unique<TagDrawer>());
    m_drawers.push_back(std::make_unique<TransformDrawer>());
    m_drawers.push_back(std::make_unique<CameraDrawer>());
    m_drawers.push_back(std::make_unique<DirectionalLightDrawer>());
    m_drawers.push_back(std::make_unique<PointLightDrawer>());
    m_drawers.push_back(std::make_unique<SpotLightDrawer>());
    m_drawers.push_back(std::make_unique<AreaLightDrawer>());
    m_drawers.push_back(std::make_unique<StaticMeshDrawer>());
    m_drawers.push_back(std::make_unique<SkeletonDrawer>());
    m_drawers.push_back(std::make_unique<AnimatorDrawer>());
    m_drawers.push_back(std::make_unique<SkinnedMeshDrawer>());
    m_drawers.push_back(std::make_unique<PBRSurfaceDrawer>());
    m_drawers.push_back(std::make_unique<MaterialParamDrawer>());
    m_drawers.push_back(std::make_unique<RigidBodyDrawer>());
    m_drawers.push_back(std::make_unique<ColliderDrawer>());
}

void InspectorPanel::OnDraw() {
    const uint32_t sel = m_hierarchy->GetSelectedEntity();
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

    for (auto& drawer : m_drawers)
        drawer->TryDraw(reg, entity, *m_scene);

    // ── Add Component ──────────────────────────────────────────────────────
    ImGui::Separator();
    if (ImGui::Button("Add Component", ImVec2(-1.f, 0.f)))
        ImGui::OpenPopup("add_component_popup");

    if (ImGui::BeginPopup("add_component_popup")) {
        auto tryAdd = [&](const char* label, bool has, auto adder) {
            if (has) ImGui::BeginDisabled();
            if (ImGui::Selectable(label) && !has) adder();
            if (has) ImGui::EndDisabled();
        };

        ImGui::SeparatorText("Rendering");
        tryAdd("Static Mesh",     reg.any_of<StaticMeshComponent>(entity),
               [&]{ reg.emplace<StaticMeshComponent>(entity); });
        tryAdd("PBR Surface",     reg.any_of<PBRSurfaceComponent>(entity),
               [&]{ reg.emplace<PBRSurfaceComponent>(entity); m_scene->MarkMaterialDirty(); });
        tryAdd("Material Params", reg.any_of<MaterialParamComponent>(entity),
               [&]{ reg.emplace<MaterialParamComponent>(entity); });

        ImGui::SeparatorText("Lighting");
        tryAdd("Directional Light", reg.any_of<DirectionalLightComponent>(entity),
               [&]{ reg.emplace<DirectionalLightComponent>(entity); });
        tryAdd("Point Light",       reg.any_of<PointLightComponent>(entity),
               [&]{ reg.emplace<PointLightComponent>(entity); });
        tryAdd("Spot Light",        reg.any_of<SpotLightComponent>(entity),
               [&]{ reg.emplace<SpotLightComponent>(entity); });
        tryAdd("Area Light",        reg.any_of<AreaLightComponent>(entity),
               [&]{ reg.emplace<AreaLightComponent>(entity); });

        ImGui::SeparatorText("Camera");
        tryAdd("Camera", reg.any_of<CameraComponent>(entity),
               [&]{ reg.emplace<CameraComponent>(entity); });

        ImGui::SeparatorText("Physics (req. restart)");
        tryAdd("Rigid Body", reg.any_of<RigidBodyComponent>(entity),
               [&]{ reg.emplace<RigidBodyComponent>(entity); });
        tryAdd("Collider",   reg.any_of<ColliderComponent>(entity),
               [&]{ reg.emplace<ColliderComponent>(entity); });

        ImGui::EndPopup();
    }
}

} // namespace StellarAlia::Editor
