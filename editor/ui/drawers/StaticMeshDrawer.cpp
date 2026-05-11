#include "ui/drawers/StaticMeshDrawer.hpp"
#include "EditorContext.hpp"
#include "function/scene/Components.hpp"
#include "function/scene/Scene.hpp"
#include "ui/drawers/DrawerHelpers.hpp"

#include <imgui.h>

namespace StellarAlia::Editor {

bool StaticMeshDrawer::TryDraw(entt::registry& reg, entt::entity entity,
                                Scene& scene, EditorContext& ctx) {
    auto* sm = reg.try_get<StaticMeshComponent>(entity);
    if (!sm) return false;
    bool open = ImGui::CollapsingHeader("Static Mesh", HeaderFlags());
    if (RemoveButton("x##rem_sm")) {
        reg.remove<StaticMeshComponent>(entity);
        scene.MarkMaterialDirty();
        return true;
    }
    if (!open) return true;

    ImGui::PushID("StaticMesh");
    bool changed = DrawAssetIDField("Mesh Asset", sm->meshAsset, "Mesh", ctx.assetReg);
    ImGui::PopID();
    if (changed) scene.MarkMaterialDirty();
    return true;
}

} // namespace StellarAlia::Editor
