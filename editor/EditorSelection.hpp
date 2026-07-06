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

    // ── Slot focus (Issue #102, persistent drill-down level) ───────────────────
    // Second viewport click on an already-selected entity focuses one material
    // slot: the viewport keeps that submesh highlighted and the Inspector slot
    // row scrolls into view with a brief flash. Any selection change clears it.
    void FocusSlot(entt::entity e, int32_t slot) {
        m_focusSlotEntity        = e;
        m_focusSlot              = slot;
        m_focusSlotScrollPending = true;
        m_focusSlotFlash         = 1.f;
    }
    void ClearSlotFocus() {
        m_focusSlotEntity        = entt::null;
        m_focusSlot              = -1;
        m_focusSlotScrollPending = false;
        m_focusSlotFlash         = 0.f;
    }
    entt::entity GetFocusedSlotEntity() const { return m_focusSlotEntity; }
    int32_t      GetFocusedSlot()       const { return m_focusSlot; }
    bool         HasPendingSlotScroll() const { return m_focusSlotScrollPending; }
    bool ConsumeSlotScrollRequest() {
        const bool p = m_focusSlotScrollPending;
        m_focusSlotScrollPending = false;
        return p;
    }
    // Flash intensity [0..1]; the slot row drawer decays it with frame time.
    float& SlotFlash() { return m_focusSlotFlash; }

    // ── Slot hover (Issue #102, frame-scoped) ──────────────────────────────────
    // Written by material-slot UI rows on hover; mirrored to the renderer and
    // cleared by EditorMode once per frame. No Notify — transient state.
    void SetHoveredSlot(entt::entity e, int32_t slot) { m_hoverSlotEntity = e; m_hoverSlot = slot; }
    void ClearHoveredSlot()                           { m_hoverSlotEntity = entt::null; m_hoverSlot = -1; }
    entt::entity GetHoveredSlotEntity() const { return m_hoverSlotEntity; }
    int32_t      GetHoveredSlot()       const { return m_hoverSlot; }

    // ── Change notifications ───────────────────────────────────────────────────
    using ChangeCallback = std::function<void()>;
    uint32_t Subscribe(ChangeCallback cb);
    void     Unsubscribe(uint32_t token);

private:
    void Notify();

    EditorSelectionType          m_type    = EditorSelectionType::None;
    entt::entity                 m_primary = entt::null;
    entt::entity                 m_hoverSlotEntity = entt::null;
    int32_t                      m_hoverSlot       = -1;
    entt::entity                 m_focusSlotEntity = entt::null;
    int32_t                      m_focusSlot       = -1;
    bool                         m_focusSlotScrollPending = false;
    float                        m_focusSlotFlash  = 0.f;
    std::unordered_set<uint32_t> m_entities;
    std::filesystem::path        m_assetPath;
    std::vector<std::pair<uint32_t, ChangeCallback>> m_subscribers;
    uint32_t                     m_nextToken = 0;
};

} // namespace StellarAlia::Editor
