#include "ui/drawers/MeshRendererDrawer.hpp"
#include "EditorContext.hpp"
#include "function/scene/Components.hpp"
#include "function/scene/Scene.hpp"
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
    if (RemoveButton("x##rem_mr")) {
        reg.remove<MeshRendererComponent>(entity);
        scene.MarkMaterialDirty();
        return true;
    }
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

    if (ImGui::TreeNode("matslots_mr", "%s", slotLabel)) {
        for (size_t i = 0; i < mr->materialSlots.size(); ++i) {
            char lbl[16];
            std::snprintf(lbl, sizeof(lbl), "[%zu]", i);
            changed |= DrawAssetIDField(lbl, mr->materialSlots[i], "Material", ctx.assetReg);
        }
        if (ImGui::SmallButton("+ Add Slot"))
            mr->materialSlots.emplace_back();
        if (!mr->materialSlots.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("- Remove Last"))
                mr->materialSlots.pop_back();
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
    if (changed) scene.MarkMaterialDirty();
    return true;
}

} // namespace StellarAlia::Editor
