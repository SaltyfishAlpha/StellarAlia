#include "ui/drawers/MeshRendererDrawer.hpp"
#include "EditorContext.hpp"
#include "EditorSelection.hpp"
#include "function/material/MaterialManager.hpp"
#include "function/scene/Components.hpp"
#include "function/scene/Scene.hpp"
#include "resource/ResourceManager.hpp"
#include "ui/drawers/DrawerHelpers.hpp"
#include "ui/drawers/SlotOverrideEditor.hpp"

#include <imgui.h>
#include <algorithm>
#include <cstdio>

namespace StellarAlia::Editor {

bool MeshRendererDrawer::TryDraw(entt::registry& reg, entt::entity entity,
                                  Scene& scene, EditorContext& ctx) {
    auto* mr = reg.try_get<MeshRendererComponent>(entity);
    if (!mr) return false;
    bool open = ImGui::CollapsingHeader("Mesh Renderer",
                    HeaderFlags(ImGuiTreeNodeFlags_DefaultOpen));
    if (RemoveComponentButton<MeshRendererComponent>("x##rem_mr", reg, entity, ctx,
            "Remove Mesh Renderer", [&scene]{ scene.MarkMaterialDirty(); }))
        return true;
    if (!open) return true;

    ImGui::PushID("MeshRenderer");
    bool changed = false;

    TrackedFieldEdit(&mr->castShadow, ctx, "Toggle Cast Shadow",
        [](bool* p){ return ImGui::Checkbox("Cast Shadow", p); },
        [&scene]{ scene.MarkMaterialDirty(); });
    ImGui::SameLine();
    TrackedFieldEdit(&mr->receiveShadow, ctx, "Toggle Receive Shadow",
        [](bool* p){ return ImGui::Checkbox("Receive Shadow", p); },
        [&scene]{ scene.MarkMaterialDirty(); });

    // Resolve the entity's mesh so slots can be labeled with the cooked
    // per-submesh material name (v6 .samesh; empty for older cooked files).
    const Resource::GPUMesh* gpuMesh = nullptr;
    if (ctx.resMgr) {
        AssetID meshId;
        if (const auto* smc = reg.try_get<StaticMeshComponent>(entity))
            meshId = smc->meshAsset;
        else if (const auto* skc = reg.try_get<SkinnedMeshComponent>(entity))
            meshId = skc->meshAsset;
        if (meshId.IsValid()) gpuMesh = ctx.resMgr->LoadMesh(meshId);
    }

    // #106: rows are per-submesh and always assignable — the slots vector is an
    // implementation detail, so the header counts submeshes, not vector size.
    const size_t rowCount =
        std::max(mr->materialSlots.size(),
                 gpuMesh ? gpuMesh->subMeshes.size() : size_t(0));
    char slotLabel[64];
    std::snprintf(slotLabel, sizeof(slotLabel), "Material Slots (%zu)", rowCount);

    // Issue #102: a fresh slot focus (viewport drill-down click) force-opens
    // the slot list so the focused row can scroll into view.
    if (ctx.selection && ctx.selection->HasPendingSlotScroll() &&
        ctx.selection->GetFocusedSlotEntity() == entity)
        ImGui::SetNextItemOpen(true);

    if (ImGui::TreeNode("matslots_mr", "%s", slotLabel)) {
        // Issue #103: the slot's effective type feeds the override add-popup.
        // Priority mirrors BuildDrawList (#106): explicit slot assignment >
        // entity-wide materialAsset replacement > cooked defaultMaterialID.
        auto slotEffectiveType = [&](size_t i) -> const MaterialType* {
            if (!ctx.matMgr || !ctx.resMgr) return nullptr;
            AssetID id;
            if (i < mr->materialSlots.size() && mr->materialSlots[i].IsValid())
                id = mr->materialSlots[i];
            else if (const auto* mo = reg.try_get<MaterialOverrideComponent>(entity);
                     mo && mo->materialAsset.IsValid())
                id = mo->materialAsset;
            else if (gpuMesh && i < gpuMesh->subMeshes.size())
                id = gpuMesh->subMeshes[i].defaultMaterialID;
            if (!id.IsValid()) return nullptr;
            MaterialInstance* inst = ctx.matMgr->LoadMaterial(id, *ctx.resMgr);
            return inst ? inst->GetType() : nullptr;
        };

        for (size_t i = 0; i < rowCount; ++i) {
            ImGui::PushID(static_cast<int>(i));
            char lbl[80];
            const char* matName =
                (gpuMesh && i < gpuMesh->subMeshes.size() &&
                 !gpuMesh->subMeshes[i].materialName.empty())
                ? gpuMesh->subMeshes[i].materialName.c_str() : "";
            if (matName[0])
                std::snprintf(lbl, sizeof(lbl), "[%zu] %s", i, matName);
            else
                std::snprintf(lbl, sizeof(lbl), "[%zu]", i);

            const bool rowFocused =
                ctx.selection && ctx.selection->GetFocusedSlotEntity() == entity &&
                ctx.selection->GetFocusedSlot() == static_cast<int32_t>(i);
            // Issue #103: viewport drill-down expands the focused row so the
            // override editor is immediately visible.
            if (rowFocused && ctx.selection->HasPendingSlotScroll())
                ImGui::SetNextItemOpen(true);

            ImGui::BeginGroup();
            // NoTreePushOnOpen: EndGroup() restores the window indent, so a
            // TreePush inside the group would corrupt the level of everything
            // after it. Children get a manual TreePush outside the group below.
            const bool openRow = ImGui::TreeNodeEx(
                "slot_row", ImGuiTreeNodeFlags_NoTreePushOnOpen, "%s", lbl);
            ImGui::SameLine();
            {
                // #106: every row is assignable — the slots vector auto-grows
                // on assignment (gaps stay invalid = cooked default), replacing
                // the old manual "+ Add Slot" flow. Clearing writes invalid,
                // which falls back to the cooked default material.
                AssetID slotId = (i < mr->materialSlots.size())
                               ? mr->materialSlots[i] : AssetID::Invalid();
                const char* fallback = matName[0] ? matName : "mesh default";
                if (DrawMaterialField("##slotAsset", slotId, fallback, ctx.assetReg)) {
                    std::vector<AssetID> newSlots = mr->materialSlots;
                    if (newSlots.size() <= i) newSlots.resize(i + 1);
                    newSlots[i] = slotId;
                    if (ctx.cmdMgr) {
                        std::vector<AssetID> oldSlots = mr->materialSlots;
                        ctx.cmdMgr->Execute(std::make_unique<CallbackCommand>(
                            "Set Material Slot",
                            [entity, newSlots](EditorContext& c){ if (auto* m = c.registry->try_get<MeshRendererComponent>(entity)) { m->materialSlots = newSlots; c.scene->MarkMaterialDirty(); } },
                            [entity, oldSlots](EditorContext& c){ if (auto* m = c.registry->try_get<MeshRendererComponent>(entity)) { m->materialSlots = oldSlots; c.scene->MarkMaterialDirty(); } }),
                            ctx);
                    } else {
                        mr->materialSlots = std::move(newSlots);
                    }
                    changed = true;
                }
            }
            ImGui::EndGroup();
            // Issue #102: hovering a slot row highlights its submesh in the viewport.
            if (ctx.selection && ImGui::IsItemHovered())
                ctx.selection->SetHoveredSlot(entity, static_cast<int32_t>(i));
            // Issue #102: focused row (viewport drill-down) — scroll once + flash.
            if (rowFocused) {
                if (ctx.selection->ConsumeSlotScrollRequest())
                    ImGui::SetScrollHereY(0.5f);
                float& flash = ctx.selection->SlotFlash();
                if (flash > 0.f) {
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                        ImGui::GetColorU32(ImVec4(1.f, 0.8f, 0.2f, 0.35f * flash)));
                    flash -= ImGui::GetIO().DeltaTime;
                }
            }
            if (openRow) {
                // Issue #103: per-slot override editing lives here — the single
                // slot UI (MaterialOverrideDrawer keeps only entity-wide parts).
                ImGui::TreePush("slot_row");
                changed |= DrawSlotOverrideEditor(reg, entity, scene, ctx,
                                                  static_cast<int32_t>(i),
                                                  slotEffectiveType(i));
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
    if (changed) scene.MarkMaterialDirty();
    return true;
}

} // namespace StellarAlia::Editor
