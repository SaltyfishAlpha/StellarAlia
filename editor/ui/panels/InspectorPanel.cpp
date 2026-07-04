#include "ui/panels/InspectorPanel.hpp"

#include "ui/AssetInspectors.hpp"
#include "ui/EditorIconCache.hpp"
#include "ui/drawers/ComponentDrawerRegistry.hpp"
#include "resource/AssetRegistry.hpp"
#include "function/scene/Scene.hpp"
#include "function/scene/Components.hpp"
#include "command/CommandManager.hpp"
#include "command/commands/ComponentCommands.hpp"

#include <imgui.h>
#include <entt/entt.hpp>

#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace StellarAlia::Editor {

namespace {

// Build a ComponentDescriptor for engine component T. makeAddCmd returns an
// undoable AddComponentCommand<T>; markMaterialDirty forwards Scene::MarkMaterialDirty
// as the command's onApplied hook (matches the drawers' remove-side effects).
template<typename T>
ComponentDescriptor MakeComponentDesc(std::string category, std::string label,
                                      bool markMaterialDirty = false) {
    std::string desc = "Add " + label;
    return ComponentDescriptor{
        std::move(category), std::move(label),
        [](entt::registry& r, entt::entity e){ return r.any_of<T>(e); },
        [markMaterialDirty, desc](entt::entity e, Scene& s)
            -> std::unique_ptr<IEditorCommand> {
            std::function<void()> onApplied;
            if (markMaterialDirty) onApplied = [&s]{ s.MarkMaterialDirty(); };
            return std::make_unique<AddComponentCommand<T>>(e, desc, std::move(onApplied));
        }
    };
}

} // namespace

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
    RegisterComponent(MakeComponentDesc<StaticMeshComponent>("Rendering", "Static Mesh"));
    RegisterComponent(MakeComponentDesc<MeshRendererComponent>("Rendering", "Mesh Renderer", /*markMaterialDirty*/true));
    RegisterComponent(MakeComponentDesc<MaterialOverrideComponent>("Rendering", "Material Override", /*markMaterialDirty*/true));

    // ── Lighting ──────────────────────────────────────────────────────────────
    RegisterComponent(MakeComponentDesc<DirectionalLightComponent>("Lighting", "Directional Light"));
    RegisterComponent(MakeComponentDesc<PointLightComponent>("Lighting", "Point Light"));
    RegisterComponent(MakeComponentDesc<SpotLightComponent>("Lighting", "Spot Light"));
    RegisterComponent(MakeComponentDesc<AreaLightComponent>("Lighting", "Area Light"));

    // ── Camera ────────────────────────────────────────────────────────────────
    RegisterComponent(MakeComponentDesc<CameraComponent>("Camera", "Camera"));

    // ── Animation ─────────────────────────────────────────────────────────────
    RegisterComponent(MakeComponentDesc<AnimatorComponent>("Animation", "Animator"));
    RegisterComponent(MakeComponentDesc<SkinnedMeshComponent>("Animation", "Skinned Mesh"));

    // ── Physics ───────────────────────────────────────────────────────────────
    RegisterComponent(MakeComponentDesc<RigidBodyComponent>("Physics (req. restart)", "Rigid Body"));
    RegisterComponent(MakeComponentDesc<ColliderComponent>("Physics (req. restart)", "Collider"));

    // ── Scripting ─────────────────────────────────────────────────────────────
    RegisterComponent(MakeComponentDesc<ScriptComponent>("Scripting", "Script"));
}

void InspectorPanel::RegisterAssetDrawers() {
    m_defaultAssetDrawer = std::make_unique<DefaultAssetInspector>();

    for (auto ext : {".txt", ".md", ".cs",
                     ".saglsl", ".saeffect", ".glsl", ".vert", ".frag", ".comp", ".hlsl", ".json",
                     ".sainputmap"})
        m_assetDrawers[ext] = std::make_unique<TextAssetInspector>();

    {
        auto matInsp = std::make_unique<MaterialAssetInspector>();
        matInsp->SetContext(m_ctx);
        m_assetDrawers[".samat"] = std::move(matInsp);
    }
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
            if (ImGui::Selectable(desc.label.c_str()) && !has) {
                auto cmd = desc.makeAddCmd(entity, *m_scene);
                if (m_ctx && m_ctx->cmdMgr) m_ctx->cmdMgr->Execute(std::move(cmd), *m_ctx);
                else if (m_ctx)             cmd->Execute(*m_ctx);
            }
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
