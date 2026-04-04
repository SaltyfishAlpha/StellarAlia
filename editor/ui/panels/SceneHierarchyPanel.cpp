#include "ui/panels/SceneHierarchyPanel.hpp"

#include "function/scene/Scene.hpp"
#include "function/scene/Components.hpp"

#include <imgui.h>
#include <entt/entt.hpp>

namespace StellarAlia::Editor {

void SceneHierarchyPanel::OnDraw() {
    auto& reg = m_scene->Registry();

    // List every entity that has a TagComponent (named entity).
    auto view = reg.view<TagComponent>();
    for (auto entity : view) {
        const auto& tag = view.get<TagComponent>(entity);
        const char* name = tag.name.empty() ? "(unnamed)" : tag.name.c_str();

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf
                                 | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (static_cast<uint32_t>(entity) == m_selected)
            flags |= ImGuiTreeNodeFlags_Selected;

        ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uint64_t>(entity)),
                          flags, "%s", name);
        if (ImGui::IsItemClicked())
            m_selected = static_cast<uint32_t>(entity);
        ImGui::TreePop();
    }

    // Click on empty space → deselect.
    if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered())
        m_selected = ~0u;
}

} // namespace StellarAlia::Editor
