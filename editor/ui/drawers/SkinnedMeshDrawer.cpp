#include "ui/drawers/SkinnedMeshDrawer.hpp"
#include "EditorContext.hpp"
#include "function/scene/Components.hpp"
#include "function/scene/Scene.hpp"
#include "ui/drawers/DrawerHelpers.hpp"

#include <imgui.h>

namespace StellarAlia::Editor {

bool SkinnedMeshDrawer::TryDraw(entt::registry& reg, entt::entity entity,
                                 Scene& scene, EditorContext& ctx) {
    auto* sm = reg.try_get<SkinnedMeshComponent>(entity);
    if (!sm) return false;
    bool open = ImGui::CollapsingHeader("Skinned Mesh", HeaderFlags());
    if (RemoveComponentButton<SkinnedMeshComponent>("x##rem_sk", reg, entity, ctx,
            "Remove Skinned Mesh", [&scene]{ scene.MarkSkinnedMeshDirty(); }))
        return true;
    if (!open) return true;

    ImGui::PushID("SkinnedMesh");
    // When mesh changes, clear runtime state and signal Application to re-prepare.
    if (DrawAssetIDField("Mesh Asset", sm->meshAsset, "Mesh", ctx.assetReg)) {
        sm->ready = false;
        scene.MarkSkinnedMeshDirty();
    }
    // #83 P1: explicit skeleton override; empty = derived from the mesh.
    if (DrawAssetIDField("Skeleton", sm->skeletonAsset, "Skeleton", ctx.assetReg)) {
        sm->ready = false;
        scene.MarkSkinnedMeshDirty();
    }
    ImGui::LabelText("Bones",  "%u", sm->boneCount);
    ImGui::LabelText("Status", "%s", sm->ready ? "Ready" : "Pending");
    ImGui::PopID();
    return true;
}

} // namespace StellarAlia::Editor
