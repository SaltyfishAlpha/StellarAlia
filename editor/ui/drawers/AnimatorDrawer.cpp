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
    if (RemoveButton("x##rem_anim")) { reg.remove<AnimatorComponent>(entity); return true; }
    if (!open) return true;
    DrawAssetIDField("Clip Asset", anim->clipAsset, "Animation", ctx.assetReg);
    ImGui::DragFloat("Time (s)", &anim->time,  0.01f, 0.f, 3600.f);
    ImGui::DragFloat("Speed",    &anim->speed, 0.01f, 0.f,   10.f);
    ImGui::Checkbox("Looping",  &anim->looping);
    ImGui::SameLine();
    ImGui::Checkbox("Playing",  &anim->playing);
    return true;
}

} // namespace StellarAlia::Editor
