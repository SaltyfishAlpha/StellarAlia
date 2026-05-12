#include "ui/drawers/TagDrawer.hpp"
#include "EditorContext.hpp"
#include "function/scene/Components.hpp"
#include "ui/drawers/DrawerHelpers.hpp"

#include <imgui.h>
#include <cstdio>

namespace StellarAlia::Editor {

bool TagDrawer::TryDraw(entt::registry& reg, entt::entity entity,
                        Scene& /*scene*/, EditorContext& ctx) {
    auto* tag = reg.try_get<TagComponent>(entity);
    if (!tag) return false;
    TrackedFieldEdit(&tag->name, ctx, "Edit Name",
        [](std::string* s){
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%s", s->c_str());
            if (ImGui::InputText("Name", buf, sizeof(buf))) { *s = buf; return true; }
            return false;
        });
    ImGui::Separator();
    return true;
}

} // namespace StellarAlia::Editor
