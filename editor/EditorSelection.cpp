#include "EditorSelection.hpp"

#include <algorithm>

namespace StellarAlia::Editor {

void EditorSelection::SelectEntity(entt::entity e, bool additive) {
    if (!additive)
        m_entities.clear();
    m_entities.insert(entt::to_integral(e));
    m_primary   = e;
    m_type      = EditorSelectionType::Entity;
    m_assetPath.clear();
    ClearSlotFocus();
    Notify();
}

void EditorSelection::SelectEntities(std::span<const entt::entity> entities) {
    m_entities.clear();
    m_primary = entt::null;
    for (entt::entity e : entities)
        m_entities.insert(entt::to_integral(e));
    m_primary = entities.empty() ? entt::null : entities.front();
    m_type    = m_entities.empty() ? EditorSelectionType::None : EditorSelectionType::Entity;
    m_assetPath.clear();
    ClearSlotFocus();
    Notify();
}

void EditorSelection::SelectAsset(const std::filesystem::path& path) {
    m_type    = path.empty() ? EditorSelectionType::None : EditorSelectionType::Asset;
    m_assetPath = path;
    m_entities.clear();
    m_primary = entt::null;
    ClearSlotFocus();
    Notify();
}

void EditorSelection::Clear() {
    m_type    = EditorSelectionType::None;
    m_primary = entt::null;
    m_entities.clear();
    m_assetPath.clear();
    ClearSlotFocus();
    Notify();
}

uint32_t EditorSelection::Subscribe(ChangeCallback cb) {
    const uint32_t token = m_nextToken++;
    m_subscribers.emplace_back(token, std::move(cb));
    return token;
}

void EditorSelection::Unsubscribe(uint32_t token) {
    m_subscribers.erase(
        std::remove_if(m_subscribers.begin(), m_subscribers.end(),
                       [token](const auto& p) { return p.first == token; }),
        m_subscribers.end());
}

void EditorSelection::Notify() {
    for (const auto& [token, cb] : m_subscribers)
        if (cb) cb();
}

} // namespace StellarAlia::Editor
