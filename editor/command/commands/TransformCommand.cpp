#include "TransformCommand.hpp"

#include "EditorContext.hpp"
#include "function/scene/Scene.hpp"

namespace StellarAlia::Editor {

ModifyTransformCommand::ModifyTransformCommand(entt::entity entity,
                                               TransformComponent before,
                                               TransformComponent after)
    : m_entity(entity), m_before(before), m_after(after) {}

void ModifyTransformCommand::Apply(EditorContext& ctx, const TransformComponent& tc) const {
    auto& reg = *ctx.registry;
    if (!reg.valid(m_entity)) return;
    reg.get<TransformComponent>(m_entity) = tc;
    ctx.scene->MarkDirty(m_entity);
}

void ModifyTransformCommand::Execute(EditorContext& ctx) {
    Apply(ctx, m_after);
}

void ModifyTransformCommand::Undo(EditorContext& ctx) {
    Apply(ctx, m_before);
}

std::string ModifyTransformCommand::GetDescription() const {
    return "Move entity";
}

} // namespace StellarAlia::Editor
