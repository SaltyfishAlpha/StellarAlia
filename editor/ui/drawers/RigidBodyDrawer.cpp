#include "ui/drawers/RigidBodyDrawer.hpp"
#include "EditorContext.hpp"
#include "function/scene/Components.hpp"
#include "ui/drawers/DrawerHelpers.hpp"

#include <imgui.h>

namespace StellarAlia::Editor {

bool RigidBodyDrawer::TryDraw(entt::registry& reg, entt::entity entity,
                               Scene& /*scene*/, EditorContext& ctx) {
    auto* rb = reg.try_get<RigidBodyComponent>(entity);
    if (!rb) return false;
    bool open = ImGui::CollapsingHeader("Rigid Body", HeaderFlags());
    if (RemoveComponentButton<RigidBodyComponent>("x##rem_rb", reg, entity, ctx, "Remove Rigid Body")) return true;
    if (!open) return true;

    const char* types[] = { "Static", "Kinematic", "Dynamic" };
    int t = static_cast<int>(rb->type);
    if (ImGui::Combo("Type", &t, types, 3)) {
        const auto newType = static_cast<RigidBodyComponent::Type>(t);
        const auto oldType = rb->type;
        if (ctx.cmdMgr) {
            ctx.cmdMgr->Execute(std::make_unique<CallbackCommand>(
                "Change Rigid Body Type",
                [entity, newType](EditorContext& c){ if (auto* r = c.registry->try_get<RigidBodyComponent>(entity)) r->type = newType; },
                [entity, oldType](EditorContext& c){ if (auto* r = c.registry->try_get<RigidBodyComponent>(entity)) r->type = oldType; }),
                ctx);
        } else {
            rb->type = newType;
        }
    }

    ImGui::BeginDisabled(rb->type != RigidBodyComponent::Type::Dynamic);
    TrackedFieldEdit(&rb->mass, ctx, "Edit Mass",
        [](float* p){ return ImGui::DragFloat("Mass", p, 0.1f, 0.001f, 10000.f); });
    ImGui::EndDisabled();
    TrackedFieldEdit(&rb->friction, ctx, "Edit Friction",
        [](float* p){ return ImGui::DragFloat("Friction", p, 0.01f, 0.f, 1.f); });
    TrackedFieldEdit(&rb->restitution, ctx, "Edit Restitution",
        [](float* p){ return ImGui::DragFloat("Restitution", p, 0.01f, 0.f, 1.f); });

    if (rb->bodyId != ~0u)
        ImGui::LabelText("Body ID", "%u", rb->bodyId & 0xFFFFu);
    else
        ImGui::LabelText("Body ID", "(not created)");
    return true;
}

} // namespace StellarAlia::Editor
