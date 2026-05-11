#include "ui/drawers/TagDrawer.hpp"
#include "EditorContext.hpp"
#include "function/scene/Components.hpp"

#include <imgui.h>
#include <cstdio>

namespace StellarAlia::Editor {

bool TagDrawer::TryDraw(entt::registry& reg, entt::entity entity,
                        Scene& /*scene*/, EditorContext& /*ctx*/) {
    auto* tag = reg.try_get<TagComponent>(entity);
    if (!tag) return false;
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s", tag->name.c_str());
    if (ImGui::InputText("Name", buf, sizeof(buf)))
        tag->name = buf;
    ImGui::Separator();
    return true;
}

} // namespace StellarAlia::Editor
