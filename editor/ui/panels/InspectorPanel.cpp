#include "ui/panels/InspectorPanel.hpp"

#include "ui/AssetInspectors.hpp"
#include "ui/EditorIconCache.hpp"
#include "ui/drawers/ComponentDrawerRegistry.hpp"
#include "resource/AssetRegistry.hpp"
#include "function/scene/Scene.hpp"
#include "function/scene/Components.hpp"

#include <imgui.h>
#include <entt/entt.hpp>

namespace StellarAlia::Editor {

InspectorPanel::InspectorPanel(EditorContext& ctx)
    : m_ctx(&ctx)
    , m_scene(ctx.scene)
    , m_selection(ctx.selection)
    , m_iconCache(ctx.iconCache)
{
    RegisterBuiltinComponents();
    RegisterAssetDrawers();
    if (m_iconCache) {
        for (auto& [ext, drawer] : m_assetDrawers) {
            if (auto* img = dynamic_cast<ImageAssetInspector*>(drawer.get()))
                img->SetIconCache(m_iconCache);
        }
    }
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
        "Rendering", "Material Override",
        [](auto& r, auto e){ return r.template any_of<MaterialOverrideComponent>(e); },
        [](auto& r, auto e, auto& s){ r.template emplace_or_replace<MaterialOverrideComponent>(e); s.MarkMaterialDirty(); }
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

    // ── Scripting ─────────────────────────────────────────────────────────────
    RegisterComponent({
        "Scripting", "Script",
        [](auto& r, auto e){ return r.template any_of<ScriptComponent>(e); },
        [](auto& r, auto e, auto& ){ r.template emplace_or_replace<ScriptComponent>(e); }
    });
}

void InspectorPanel::RegisterAssetDrawers() {
    m_defaultAssetDrawer = std::make_unique<DefaultAssetInspector>();

    for (auto ext : {".txt", ".md", ".cs",
                     ".saglsl", ".glsl", ".vert", ".frag", ".comp", ".hlsl", ".json"})
        m_assetDrawers[ext] = std::make_unique<TextAssetInspector>();

    m_assetDrawers[".samat"]   = std::make_unique<MaterialAssetInspector>();
    m_assetDrawers[".sascene"] = std::make_unique<SceneAssetInspector>();

    for (auto ext : {".gltf", ".glb", ".fbx", ".obj"})
        m_assetDrawers[ext] = std::make_unique<ModelAssetInspector>();

    for (auto ext : {".png", ".jpg", ".jpeg", ".hdr", ".tga", ".bmp", ".exr"})
        m_assetDrawers[ext] = std::make_unique<ImageAssetInspector>();
}



void InspectorPanel::OnDraw() {
    if (!m_selection) { ImGui::TextDisabled("Nothing selected"); return; }

    const auto selType   = m_selection->GetType();
    const uint32_t selEntity =
        (selType == EditorSelectionType::Entity)
            ? static_cast<uint32_t>(m_selection->GetPrimaryEntity())
            : ~0u;
    const std::filesystem::path selAsset =
        (selType == EditorSelectionType::Asset)
            ? m_selection->GetSelectedAsset()
            : std::filesystem::path{};

    if (selType == EditorSelectionType::Entity)
        DrawEntityInspector(selEntity);
    else if (selType == EditorSelectionType::Asset && !selAsset.empty())
        DrawAssetInspector(selAsset);
    else
        ImGui::TextDisabled("Nothing selected");
}

void InspectorPanel::DrawEntityInspector(uint32_t sel) {
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

    ImGui::PushItemWidth(-150.f);
    if (m_ctx && m_ctx->drawerRegistry)
        m_ctx->drawerRegistry->DrawAll(reg, entity, *m_scene, *m_ctx);
    ImGui::PopItemWidth();

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

void InspectorPanel::DrawAssetInspector(const std::filesystem::path& path) {
    const std::string ext = path.extension().string();
    auto it = m_assetDrawers.find(ext);
    IAssetInspector* drawer = (it != m_assetDrawers.end())
                            ? it->second.get()
                            : m_defaultAssetDrawer.get();
    drawer->Draw(path);
}

} // namespace StellarAlia::Editor
