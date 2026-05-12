#include "command/commands/FieldCommands.hpp"

namespace StellarAlia::Editor {

template<typename T>
SetFieldCommand<T>::SetFieldCommand(T* target, T oldValue, T newValue, std::string description,
                                     OnApplied onApplied)
    : m_target(target)
    , m_oldValue(std::move(oldValue))
    , m_newValue(std::move(newValue))
    , m_description(std::move(description))
    , m_onApplied(std::move(onApplied)) {}

template<typename T>
void SetFieldCommand<T>::Execute(EditorContext&) {
    if (m_target) *m_target = m_newValue;
    if (m_onApplied) m_onApplied();
}

template<typename T>
void SetFieldCommand<T>::Undo(EditorContext&) {
    if (m_target) *m_target = m_oldValue;
    if (m_onApplied) m_onApplied();
}

template class SetFieldCommand<bool>;
template class SetFieldCommand<int>;
template class SetFieldCommand<float>;
template class SetFieldCommand<glm::vec2>;
template class SetFieldCommand<glm::vec3>;
template class SetFieldCommand<glm::vec4>;
template class SetFieldCommand<glm::quat>;
template class SetFieldCommand<std::string>;
template class SetFieldCommand<AssetID>;
template class SetFieldCommand<std::uint64_t>;

} // namespace StellarAlia::Editor
