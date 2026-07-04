#include "ui/drawers/MeshRendererDrawer.hpp"
#include "EditorContext.hpp"
#include "function/scene/Components.hpp"
#include "function/scene/Scene.hpp"
#include "resource/ResourceManager.hpp"
#include "ui/drawers/DrawerHelpers.hpp"

#include <imgui.h>
#include <cstdio>

namespace StellarAlia::Editor {

bool MeshRendererDrawer::TryDraw(entt::registry& reg, entt::entity entity,
                                  Scene& scene, EditorContext& ctx) {
    auto* mr = reg.try_get<MeshRendererComponent>(entity);
    if (!mr) return false;
    bool open = ImGui::CollapsingHeader("Mesh Renderer",
                    HeaderFlags(ImGuiTreeNodeFlags_DefaultOpen));
    if (RemoveComponentButton<MeshRendererComponent>("x##rem_mr", reg, entity, ctx,
            "Remove Mesh Renderer", [&scene]{ scene.MarkMaterialDirty(); }))
        return true;
    if (!open) return true;

    ImGui::PushID("MeshRenderer");
    bool changed = false;

    TrackedFieldEdit(&mr->castShadow, ctx, "Toggle Cast Shadow",
        [](bool* p){ return ImGui::Checkbox("Cast Shadow", p); },
        [&scene]{ scene.MarkMaterialDirty(); });
    ImGui::SameLine();
    TrackedFieldEdit(&mr->receiveShadow, ctx, "Toggle Receive Shadow",
        [](bool* p){ return ImGui::Checkbox("Receive Shadow", p); },
        [&scene]{ scene.MarkMaterialDirty(); });

    char slotLabel[64];
    if (mr->materialSlots.empty())
        std::snprintf(slotLabel, sizeof(slotLabel), "Material Slots (0, using mesh defaults)");
    else
        std::snprintf(slotLabel, sizeof(slotLabel), "Material Slots (%zu)", mr->materialSlots.size());

    // Resolve the entity's mesh so slots can be labeled with the cooked
    // per-submesh material name (v6 .samesh; empty for older cooked files).
    const Resource::GPUMesh* gpuMesh = nullptr;
    if (ctx.resMgr) {
        AssetID meshId;
        if (const auto* smc = reg.try_get<StaticMeshComponent>(entity))
            meshId = smc->meshAsset;
        else if (const auto* skc = reg.try_get<SkinnedMeshComponent>(entity))
            meshId = skc->meshAsset;
        if (meshId.IsValid()) gpuMesh = ctx.resMgr->LoadMesh(meshId);
    }

    if (ImGui::TreeNode("matslots_mr", "%s", slotLabel)) {
        for (size_t i = 0; i < mr->materialSlots.size(); ++i) {
            char lbl[80];
            const char* matName =
                (gpuMesh && i < gpuMesh->subMeshes.size() &&
                 !gpuMesh->subMeshes[i].materialName.empty())
                ? gpuMesh->subMeshes[i].materialName.c_str() : "";
            if (matName[0])
                std::snprintf(lbl, sizeof(lbl), "[%zu] %s", i, matName);
            else
                std::snprintf(lbl, sizeof(lbl), "[%zu]", i);
            changed |= DrawAssetIDField(lbl, mr->materialSlots[i], "Material", ctx.assetReg);
        }
        if (ImGui::SmallButton("+ Add Slot")) {
            if (ctx.cmdMgr) {
                ctx.cmdMgr->Execute(std::make_unique<CallbackCommand>(
                    "Add Material Slot",
                    [entity](EditorContext& c){ if (auto* m = c.registry->try_get<MeshRendererComponent>(entity)) { m->materialSlots.emplace_back(); c.scene->MarkMaterialDirty(); } },
                    [entity](EditorContext& c){ if (auto* m = c.registry->try_get<MeshRendererComponent>(entity)) { if (!m->materialSlots.empty()) m->materialSlots.pop_back(); c.scene->MarkMaterialDirty(); } }),
                    ctx);
            } else {
                mr->materialSlots.emplace_back();
                scene.MarkMaterialDirty();
            }
        }
        if (!mr->materialSlots.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("- Remove Last")) {
                if (ctx.cmdMgr) {
                    const AssetID savedSlot = mr->materialSlots.back();
                    ctx.cmdMgr->Execute(std::make_unique<CallbackCommand>(
                        "Remove Material Slot",
                        [entity](EditorContext& c){ if (auto* m = c.registry->try_get<MeshRendererComponent>(entity)) { if (!m->materialSlots.empty()) m->materialSlots.pop_back(); c.scene->MarkMaterialDirty(); } },
                        [entity, savedSlot](EditorContext& c){ if (auto* m = c.registry->try_get<MeshRendererComponent>(entity)) { m->materialSlots.push_back(savedSlot); c.scene->MarkMaterialDirty(); } }),
                        ctx);
                } else {
                    mr->materialSlots.pop_back();
                    scene.MarkMaterialDirty();
                }
            }
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
    if (changed) scene.MarkMaterialDirty();
    return true;
}

} // namespace StellarAlia::Editor
