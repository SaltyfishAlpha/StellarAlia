#pragma once

#include "command/IEditorCommand.hpp"
#include "core/asset/AssetID.hpp"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/quaternion.hpp>
#include <functional>
#include <string>
#include <utility>

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// SetFieldCommand<T> — generic single-field set.
//
// target is the stable address of a field; the caller owns the lifetime.
// Typical use: pointer into an entt::registry component, which stays stable
// until the component is removed. If the target is invalidated before Undo()
// runs, behaviour is undefined — CommandManager::Clear() should be called on
// scene switches / play boundaries to avoid stale-pointer undo records.
//
// onApplied fires after both Execute() and Undo() — use it to notify side
// effects that aren't captured in the field itself (e.g. Scene::MarkDirty so
// the BVH refreshes when the user hits Ctrl+Z on a Transform field).
// ─────────────────────────────────────────────────────────────────────────────
template<typename T>
class SetFieldCommand final : public IEditorCommand {
public:
    using OnApplied = std::function<void()>;

    SetFieldCommand(T* target, T oldValue, T newValue, std::string description,
                    OnApplied onApplied = {});

    void Execute(EditorContext& ctx) override;
    void Undo   (EditorContext& ctx) override;
    [[nodiscard]] std::string GetDescription() const override { return m_description; }

private:
    T*          m_target;
    T           m_oldValue;
    T           m_newValue;
    std::string m_description;
    OnApplied   m_onApplied;
};

// Definitions live in FieldCommands.cpp.
extern template class SetFieldCommand<bool>;
extern template class SetFieldCommand<int>;
extern template class SetFieldCommand<float>;
extern template class SetFieldCommand<glm::vec2>;
extern template class SetFieldCommand<glm::vec3>;
extern template class SetFieldCommand<glm::vec4>;
extern template class SetFieldCommand<glm::quat>;
extern template class SetFieldCommand<std::string>;
extern template class SetFieldCommand<AssetID>;
extern template class SetFieldCommand<std::uint64_t>;

} // namespace StellarAlia::Editor
