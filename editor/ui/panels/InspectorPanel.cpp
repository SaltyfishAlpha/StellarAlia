#include "ui/panels/InspectorPanel.hpp"

#include "ui/ComponentDrawers.hpp"
#include "resource/AssetRegistry.hpp"
#include "function/scene/Scene.hpp"
#include "function/scene/Components.hpp"

#include <imgui.h>
#include <entt/entt.hpp>

namespace StellarAlia::Editor {

InspectorPanel::InspectorPanel(Scene& scene,
                               const SceneHierarchyPanel& hierarchy,
                               const Resource::AssetRegistry* registry)
    : m_scene(&scene), m_hierarchy(&hierarchy), m_registry(registry)
{
    RegisterDrawers();
    RegisterBuiltinComponents();
}

void InspectorPanel::RegisterComponent(ComponentDescriptor desc) {
    m_addableComponents.push_back(std::move(desc));
}

void InspectorPanel::RegisterBuiltinComponents() {
    // ── Rendering ─────────────────────────────────────────────────────────────
    RegisterComponent({
        "Rendering", "Static Mesh",
        [](auto& r, auto e){ return r.template any_of<StaticMeshComponent>(e); },
        [](auto& r, auto e, auto& ){ r.template emplace_or_replace<StaticMeshComponent>(e); }
    });
    RegisterComponent({
        "Rendering", "Mesh Renderer",
        [](auto& r, auto e){ return r.template any_of<MeshRendererComponent>(e); },
        [](auto& r, auto e, auto& s){ r.template emplace_or_replace<MeshRendererComponent>(e); s.MarkMaterialDirty(); }
    });
    RegisterComponent({
        "Rendering", "PBR Surface",
        [](auto& r, auto e){ return r.template any_of<PBRSurfaceComponent>(e); },
        [](auto& r, auto e, auto& s){ r.template emplace_or_replace<PBRSurfaceComponent>(e); s.MarkMaterialDirty(); }
    });
    RegisterComponent({
        "Rendering", "Material Params",
        [](auto& r, auto e){ return r.template any_of<MaterialParamComponent>(e); },
        [](auto& r, auto e, auto& ){ r.template emplace_or_replace<MaterialParamComponent>(e); }
    });

    // ── Lighting ──────────────────────────────────────────────────────────────
    RegisterComponent({
        "Lighting", "Directional Light",
        [](auto& r, auto e){ return r.template any_of<DirectionalLightComponent>(e); },
        [](auto& r, auto e, auto& ){ r.template emplace_or_replace<DirectionalLightComponent>(e); }
    });
    RegisterComponent({
        "Lighting", "Point Light",
        [](auto& r, auto e){ return r.template any_of<PointLightComponent>(e); },
        [](auto& r, auto e, auto& ){ r.template emplace_or_replace<PointLightComponent>(e); }
    });
    RegisterComponent({
        "Lighting", "Spot Light",
        [](auto& r, auto e){ return r.template any_of<SpotLightComponent>(e); },
        [](auto& r, auto e, auto& ){ r.template emplace_or_replace<SpotLightComponent>(e); }
    });
    RegisterComponent({
        "Lighting", "Area Light",
        [](auto& r, auto e){ return r.template any_of<AreaLightComponent>(e); },
        [](auto& r, auto e, auto& ){ r.template emplace_or_replace<AreaLightComponent>(e); }
    });

    // ── Camera ────────────────────────────────────────────────────────────────
    RegisterComponent({
        "Camera", "Camera",
        [](auto& r, auto e){ return r.template any_of<CameraComponent>(e); },
        [](auto& r, auto e, auto& ){ r.template emplace_or_replace<CameraComponent>(e); }
    });

    // ── Animation ─────────────────────────────────────────────────────────────
    RegisterComponent({
        "Animation", "Animator",
        [](auto& r, auto e){ return r.template any_of<AnimatorComponent>(e); },
        [](auto& r, auto e, auto& ){ r.template emplace_or_replace<AnimatorComponent>(e); }
    });
    RegisterComponent({
        "Animation", "Skinned Mesh",
        [](auto& r, auto e){ return r.template any_of<SkinnedMeshComponent>(e); },
        [](auto& r, auto e, auto& ){ r.template emplace_or_replace<SkinnedMeshComponent>(e); }
    });

    // ── Physics ───────────────────────────────────────────────────────────────
    RegisterComponent({
        "Physics (req. restart)", "Rigid Body",
        [](auto& r, auto e){ return r.template any_of<RigidBodyComponent>(e); },
        [](auto& r, auto e, auto& ){ r.template emplace_or_replace<RigidBodyComponent>(e); }
    });
    RegisterComponent({
        "Physics (req. restart)", "Collider",
        [](auto& r, auto e){ return r.template any_of<ColliderComponent>(e); },
        [](auto& r, auto e, auto& ){ r.template emplace_or_replace<ColliderComponent>(e); }
    });
}

void InspectorPanel::RegisterDrawers() {
    m_drawers.push_back(std::make_unique<TagDrawer>());
    m_drawers.push_back(std::make_unique<TransformDrawer>());
    m_drawers.push_back(std::make_unique<CameraDrawer>());
    m_drawers.push_back(std::make_unique<DirectionalLightDrawer>());
    m_drawers.push_back(std::make_unique<PointLightDrawer>());
    m_drawers.push_back(std::make_unique<SpotLightDrawer>());
    m_drawers.push_back(std::make_unique<AreaLightDrawer>());
    m_drawers.push_back(std::make_unique<StaticMeshDrawer>(m_registry));
    m_drawers.push_back(std::make_unique<MeshRendererDrawer>(m_registry));
    m_drawers.push_back(std::make_unique<AnimatorDrawer>(m_registry));
    m_drawers.push_back(std::make_unique<SkinnedMeshDrawer>(m_registry));
    m_drawers.push_back(std::make_unique<PBRSurfaceDrawer>(m_registry));
    m_drawers.push_back(std::make_unique<MaterialParamDrawer>(m_registry));
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
        std::string lastCategory;
        for (const auto& desc : m_addableComponents) {
            if (desc.category != lastCategory) {
                ImGui::SeparatorText(desc.category.c_str());
                lastCategory = desc.category;
            }
            const bool has = desc.hasComp(reg, entity);
            if (has) ImGui::BeginDisabled();
            if (ImGui::Selectable(desc.label.c_str()) && !has)
                desc.addComp(reg, entity, *m_scene);
            if (has) ImGui::EndDisabled();
        }
        ImGui::EndPopup();
    }
}

} // namespace StellarAlia::Editor
