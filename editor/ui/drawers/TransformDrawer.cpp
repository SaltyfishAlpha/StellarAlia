#include "ui/drawers/TransformDrawer.hpp"
#include "EditorContext.hpp"
#include "function/scene/Components.hpp"
#include "function/scene/Scene.hpp"
#include "ui/IconsFontAwesome6.h"

#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

namespace StellarAlia::Editor {

bool TransformDrawer::TryDraw(entt::registry& reg, entt::entity entity,
                               Scene& scene, EditorContext& ctx) {
    auto* tr = reg.try_get<TransformComponent>(entity);
    if (!tr) return false;
    if (!ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        return true;

    bool changed = false;
    changed |= ImGui::DragFloat3("Position", glm::value_ptr(tr->position), 0.1f);

    // Re-seed cached Euler only when selection changes — avoids quat→euler
    // round-trip each frame which clamps Y to [-90,+90] via asin.
    const uint32_t sel = static_cast<uint32_t>(entity);
    if (sel != m_cachedEulerEntity) {
        m_cachedEuler       = glm::degrees(glm::eulerAngles(tr->rotation));
        m_cachedEulerEntity = sel;
    }
    if (ImGui::DragFloat3("Rotation (deg)", glm::value_ptr(m_cachedEuler), 0.5f)) {
        tr->rotation = glm::quat(glm::radians(m_cachedEuler));
        changed = true;
    }

    {
        const float btnSz = ImGui::GetFrameHeight();

        ImGui::PushID("##scale_lock");
        const bool wasLocked = m_scaleLocked;
        if (wasLocked)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.90f, 1.f));
        const ImVec2 btnPos = ImGui::GetCursorScreenPos();
        if (ImGui::Button("##btn", ImVec2(btnSz, btnSz)))
            m_scaleLocked = !m_scaleLocked;
        if (wasLocked)
            ImGui::PopStyleColor();
        if (ctx.iconFont) {
            const char*  icon   = wasLocked ? ICON_FA_LINK : ICON_FA_LINK_SLASH;
            const float  iconPx = btnSz - 2.f * ImGui::GetStyle().FramePadding.y;
            const ImVec2 tsz    = ctx.iconFont->CalcTextSizeA(iconPx, FLT_MAX, 0.f, icon);
            ImGui::GetWindowDrawList()->AddText(
                ctx.iconFont, iconPx,
                ImVec2(btnPos.x + (btnSz - tsz.x) * 0.5f,
                       btnPos.y + (btnSz - tsz.y) * 0.5f),
                IM_COL32_WHITE, icon);
        }
        ImGui::PopID();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", m_scaleLocked
                ? "Scale: proportional  (click to unlock)"
                : "Scale: independent   (click to lock)");

        ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::SetNextItemWidth(ImGui::CalcItemWidth()
                                - btnSz - ImGui::GetStyle().ItemInnerSpacing.x);

        const glm::vec3 prev = tr->scale;
        if (ImGui::DragFloat3("Scale", glm::value_ptr(tr->scale), 0.01f, 0.001f, 1000.f)) {
            if (m_scaleLocked) {
                const glm::vec3 d = tr->scale - prev;
                int axis = (std::abs(d[1]) > std::abs(d[0])) ? 1 : 0;
                if (std::abs(d[2]) > std::abs(d[axis])) axis = 2;
                const float ratio = std::abs(prev[axis]) > 1e-6f
                                  ? tr->scale[axis] / prev[axis] : 1.f;
                tr->scale = prev * ratio;
            }
            changed = true;
        }
    }
    if (changed) {
        scene.MarkDirty(entity);
        scene.MarkMaterialDirty();
    }
    return true;
}

} // namespace StellarAlia::Editor
