#pragma once

#include "resource/AssetRegistry.hpp"
#include "core/asset/AssetID.hpp"
#include "EditorContext.hpp"
#include "command/CommandManager.hpp"
#include "command/commands/FieldCommands.hpp"
#include "command/commands/ComponentCommands.hpp"
#include "ui/AssetDragPayload.hpp"

#include <entt/entt.hpp>
#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace StellarAlia::Editor {

// Read-only fallback: shows the UUID prefix as a label.
inline void DrawAssetID(const char* label, const AssetID& id) {
    if (id.IsValid()) {
        std::string s = id.ToString();
        ImGui::LabelText(label, "%.8s\xe2\x80\xa6", s.c_str());
    } else {
        ImGui::LabelText(label, "(none)");
    }
}

// Interactive AssetID picker.
// Returns true if the id was changed.
// filterType — "Mesh", "Texture", etc.; nullptr or "" = show all.
inline bool DrawAssetIDField(const char* label, AssetID& id,
                             const char* filterType,
                             const Resource::AssetRegistry* registry)
{
    if (!registry) {
        DrawAssetID(label, id);
        return false;
    }

    ImGui::PushID(label);
    bool changed = false;

    const char* btnLabel = "(none)";
    std::string nameStorage;
    if (id.IsValid()) {
        if (const auto* e = registry->FindByID(id)) {
            nameStorage = e->name;
            btnLabel    = nameStorage.c_str();
        } else {
            nameStorage = id.ToString().substr(0, 8) + "\xe2\x80\xa6";
            btnLabel    = nameStorage.c_str();
        }
    }

    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    const float clearW   = id.IsValid() ? 26.f : 0.f;
    const float btnWidth = std::max(10.f, ImGui::GetContentRegionAvail().x - clearW);
    if (ImGui::Button(btnLabel, ImVec2(btnWidth, 0)))
        ImGui::OpenPopup("##asset_pick");

    // Drop target on the picker button — accept SAASSET payload from AssetsPanel.
    // Direct write; callers' existing `if (changed)` path handles dirty / undo
    // (matches how the picker itself reports changes).
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("SAASSET")) {
            if (pl->DataSize >= static_cast<int>(sizeof(AssetDragPayload))) {
                const auto& p = *static_cast<const AssetDragPayload*>(pl->Data);
                const bool typeOK = (filterType == nullptr || filterType[0] == '\0' ||
                                     std::strncmp(p.type, filterType, sizeof(p.type)) == 0);
                if (p.id.IsValid() && typeOK && id != p.id) {
                    id      = p.id;
                    changed = true;
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (id.IsValid()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("\xc3\x97")) {
            id      = AssetID::Invalid();
            changed = true;
        }
    }

    if (ImGui::BeginPopup("##asset_pick")) {
        static char filter[128] = {};
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##flt", "Filter\xe2\x80\xa6", filter, sizeof(filter));
        ImGui::Separator();
        ImGui::BeginChild("##list", ImVec2(280, 200), false);

        const std::string_view ft = filterType ? filterType : "";
        for (const auto* e : registry->EntriesByType(ft)) {
            if (filter[0] != '\0') {
                std::string haystack = e->name;
                std::string needle   = filter;
                auto tolowerChar = [](unsigned char c){ return static_cast<char>(::tolower(c)); };
                std::transform(haystack.begin(), haystack.end(), haystack.begin(), tolowerChar);
                std::transform(needle.begin(),   needle.end(),   needle.begin(),   tolowerChar);
                if (haystack.find(needle) == std::string::npos)
                    continue;
            }
            const bool selected = (e->id == id);
            if (ImGui::Selectable(e->name.c_str(), selected)) {
                id      = e->id;
                changed = true;
                filter[0] = '\0';
                ImGui::CloseCurrentPopup();
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }

        ImGui::EndChild();
        ImGui::EndPopup();
    }

    ImGui::PopID();
    return changed;
}

// Render a small "×" remove button at the right edge; returns true when clicked.
// Call immediately after CollapsingHeader() with AllowOverlap flag.
inline bool RemoveButton(const char* id) {
    float btnX = ImGui::GetWindowWidth() - 28.f;
    ImGui::SameLine(btnX > 0.f ? btnX : 0.f);
    return ImGui::SmallButton(id);
}

// Returns ImGuiTreeNodeFlags with AllowOverlap always set.
inline ImGuiTreeNodeFlags HeaderFlags(ImGuiTreeNodeFlags extra = 0) {
    return ImGuiTreeNodeFlags_AllowOverlap | extra;
}

// ─────────────────────────────────────────────────────────────────────────────
// RemoveComponentButton<T> — draws the right-edge "×" button and, when clicked,
// removes component T from the entity via an undoable RemoveComponentCommand<T>.
// Falls back to a direct reg.remove<T>() when no CommandManager is available
// (mirrors AcceptAssetIDDrop / TrackedFieldEdit).
//
// onApplied fires after both the removal and its undo — pass the same side
// effect the drawer used to run inline (e.g. Scene::MarkMaterialDirty), so the
// restored component is reflected in dependent systems.
//
// Returns true when the component was removed (caller should stop drawing it).
// ─────────────────────────────────────────────────────────────────────────────
template<typename T>
inline bool RemoveComponentButton(const char* id,
                                  entt::registry& reg, entt::entity entity,
                                  EditorContext& ctx, const char* description,
                                  std::function<void()> onApplied = {})
{
    if (!RemoveButton(id)) return false;
    if (ctx.cmdMgr) {
        ctx.cmdMgr->Execute(
            std::make_unique<RemoveComponentCommand<T>>(
                entity, description ? description : "Remove Component",
                std::move(onApplied)),
            ctx);
    } else {
        reg.remove<T>(entity);
        if (onApplied) onApplied();
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// AcceptAssetIDDrop — wrap an ImGui drop target that accepts SAASSET payloads
// (from AssetsPanel), filters by AssetEntry::type, writes the dropped AssetID
// into outId, and pushes a SetFieldCommand<AssetID> so the drop is undoable.
//
// Call this immediately after an ImGui item that should accept the drop:
//     DrawAssetIDField(...);
//     AcceptAssetIDDrop(field, "Mesh", ctx, "Drop Mesh");
//
// filterType == nullptr or "" matches any asset type.
// Returns true if the field was changed.
// ─────────────────────────────────────────────────────────────────────────────
inline bool AcceptAssetIDDrop(AssetID& outId,
                              const char* filterType,
                              EditorContext& ctx,
                              const char* commandDesc = "Set Asset",
                              std::function<void()> onApplied = {})
{
    if (!ImGui::BeginDragDropTarget()) return false;
    bool changed = false;
    if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("SAASSET")) {
        if (pl->DataSize >= static_cast<int>(sizeof(AssetDragPayload))) {
            const auto& p = *static_cast<const AssetDragPayload*>(pl->Data);
            const bool typeOK = (filterType == nullptr || filterType[0] == '\0' ||
                                 std::strncmp(p.type, filterType, sizeof(p.type)) == 0);
            if (p.id.IsValid() && typeOK && outId != p.id) {
                AssetID oldId = outId;
                if (ctx.cmdMgr) {
                    ctx.cmdMgr->Execute(
                        std::make_unique<SetFieldCommand<AssetID>>(
                            &outId, oldId, p.id,
                            commandDesc ? commandDesc : "Set Asset",
                            std::move(onApplied)),
                        ctx);
                } else {
                    outId = p.id;
                    if (onApplied) onApplied();
                }
                changed = true;
            }
        }
    }
    ImGui::EndDragDropTarget();
    return changed;
}

// ─────────────────────────────────────────────────────────────────────────────
// TrackedFieldEdit — wrap any single ImGui control so its edit becomes a single
// undoable SetFieldCommand<T>.
//
// Usage:
//   TrackedFieldEdit(&tr->position, ctx, "Edit Position",
//       [](glm::vec3* p){ return ImGui::DragFloat3("Position", glm::value_ptr(*p), 0.1f); },
//       [&]{ scene.MarkDirty(entity); });   // optional onApplied (Undo replay)
//
// draw(T*) must call exactly one ImGui control on *target — TrackedFieldEdit
// inspects IsItemActivated/IsItemDeactivatedAfterEdit on the LAST item to
// snapshot pre-edit value and to commit one command per drag (continuous
// drags collapse into a single undo record).
//
// onApplied is forwarded into the SetFieldCommand so Undo/Redo fires the
// caller's side-effect hook (e.g. dirty-flag propagation).
// ─────────────────────────────────────────────────────────────────────────────
template<typename T, typename DrawFn>
bool TrackedFieldEdit(T* target,
                      EditorContext& ctx,
                      std::string    description,
                      DrawFn         draw,
                      std::function<void()> onApplied = {})
{
    // Per-instantiation single-slot record. ImGui guarantees only one item is
    // "active" globally per frame, so one slot per T is sufficient.
    thread_local T*  s_activeTarget = nullptr;
    thread_local T   s_oldValue{};

    // Snapshot before draw — IsItemActivated fires on the same frame the user
    // first interacts, by which point draw() may have already mutated *target.
    T preDraw = *target;
    const bool changed = draw(target);

    if (ImGui::IsItemActivated()) {
        s_activeTarget = target;
        s_oldValue     = std::move(preDraw);
    }

    if (ImGui::IsItemDeactivatedAfterEdit() && s_activeTarget == target) {
        T newValue = *target;
        s_activeTarget = nullptr;
        if (ctx.cmdMgr && s_oldValue != newValue) {
            ctx.cmdMgr->Execute(
                std::make_unique<SetFieldCommand<T>>(
                    target,
                    std::move(s_oldValue),
                    std::move(newValue),
                    std::move(description),
                    std::move(onApplied)),
                ctx);
        }
    } else if (ImGui::IsItemDeactivated() && s_activeTarget == target) {
        // User abandoned without editing (escape / click-away). Drop snapshot.
        s_activeTarget = nullptr;
    }

    return changed;
}

} // namespace StellarAlia::Editor
