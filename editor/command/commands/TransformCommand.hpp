#pragma once

#include "command/IEditorCommand.hpp"
#include "function/scene/Components.hpp"

#include <entt/entt.hpp>
#include <string>

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// ModifyTransformCommand — records before/after TransformComponent.
// Created at gizmo drag-end; merged with the next drag if same entity (see
// CommandManager comment for future merging).
// ─────────────────────────────────────────────────────────────────────────────
class ModifyTransformCommand final : public IEditorCommand {
public:
    ModifyTransformCommand(entt::entity entity,
                           TransformComponent before,
                           TransformComponent after);

    void Execute(EditorContext& ctx) override;
    void Undo(EditorContext& ctx) override;
    [[nodiscard]] std::string GetDescription() const override;

private:
    void Apply(EditorContext& ctx, const TransformComponent& tc) const;

    entt::entity      m_entity;
    TransformComponent m_before;
    TransformComponent m_after;
};

} // namespace StellarAlia::Editor
