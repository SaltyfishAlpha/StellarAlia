#include "ui/drawers/AnimatorDrawer.hpp"
#include "EditorContext.hpp"
#include "function/scene/Components.hpp"
#include "ui/drawers/DrawerHelpers.hpp"

#include <imgui.h>

namespace StellarAlia::Editor {

bool AnimatorDrawer::TryDraw(entt::registry& reg, entt::entity entity,
                              Scene& /*scene*/, EditorContext& ctx) {
    auto* anim = reg.try_get<AnimatorComponent>(entity);
    if (!anim) return false;
    bool open = ImGui::CollapsingHeader("Animator",
                    HeaderFlags(ImGuiTreeNodeFlags_DefaultOpen));
    if (RemoveComponentButton<AnimatorComponent>("x##rem_anim", reg, entity, ctx, "Remove Animator")) return true;
    if (!open) return true;
    DrawAssetIDField("Clip Asset", anim->clipAsset, "Animation", ctx.assetReg);
    TrackedFieldEdit(&anim->time, ctx, "Edit Time",
        [](float* p){ return ImGui::DragFloat("Time (s)", p, 0.01f, 0.f, 3600.f); });
    TrackedFieldEdit(&anim->speed, ctx, "Edit Speed",
        [](float* p){ return ImGui::DragFloat("Speed", p, 0.01f, 0.f, 10.f); });
    TrackedFieldEdit(&anim->looping, ctx, "Toggle Looping",
        [](bool* p){ return ImGui::Checkbox("Looping", p); });
    ImGui::SameLine();
    TrackedFieldEdit(&anim->playing, ctx, "Toggle Playing",
        [](bool* p){ return ImGui::Checkbox("Playing", p); });
    return true;
}

} // namespace StellarAlia::Editor
