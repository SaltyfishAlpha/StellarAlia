#pragma once

#include <entt/entt.hpp>
#include <filesystem>
#include <functional>
#include <span>
#include <unordered_set>
#include <vector>

namespace StellarAlia::Editor {

enum class EditorSelectionType { None, Entity, Asset };

class EditorSelection {
public:
    // ── Write ──────────────────────────────────────────────────────────────────
    void SelectEntity(entt::entity e, bool additive = false);
    void SelectEntities(std::span<const entt::entity> entities);
    void SelectAsset(const std::filesystem::path& path);
    void Clear();

    // ── Read ───────────────────────────────────────────────────────────────────
    EditorSelectionType                 GetType()          const { return m_type; }
    entt::entity                        GetPrimaryEntity() const { return m_primary; }
    const std::unordered_set<uint32_t>& GetEntitySet()     const { return m_entities; }
    const std::filesystem::path&        GetSelectedAsset() const { return m_assetPath; }
    bool HasEntity() const { return m_type == EditorSelectionType::Entity; }
    bool HasAsset()  const { return m_type == EditorSelectionType::Asset; }

    // ── Change notifications ───────────────────────────────────────────────────
    using ChangeCallback = std::function<void()>;
    uint32_t Subscribe(ChangeCallback cb);
    void     Unsubscribe(uint32_t token);

private:
    void Notify();

    EditorSelectionType          m_type    = EditorSelectionType::None;
    entt::entity                 m_primary = entt::null;
    std::unordered_set<uint32_t> m_entities;
    std::filesystem::path        m_assetPath;
    std::vector<std::pair<uint32_t, ChangeCallback>> m_subscribers;
    uint32_t                     m_nextToken = 0;
};

} // namespace StellarAlia::Editor
