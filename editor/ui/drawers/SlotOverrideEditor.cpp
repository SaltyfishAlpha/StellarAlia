#include "ui/drawers/SlotOverrideEditor.hpp"

#include "EditorContext.hpp"
#include "function/scene/Components.hpp"
#include "function/scene/Scene.hpp"
#include "function/material/MaterialManager.hpp"
#include "function/material/MaterialType.hpp"
#include "resource/AssetRegistry.hpp"
#include "ui/drawers/DrawerHelpers.hpp"
#include "ui/drawers/ParamWidgets.hpp"

#include <imgui.h>

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_set>

namespace StellarAlia::Editor {

namespace {

// The first edit on a slot may need to create the MaterialOverrideComponent
// and/or the slotOverrides entry. Commands record what they created in shared
// flags so undo tears down exactly that (redo re-evaluates from scratch).
MaterialOverrideComponent* EnsureSlotEntry(EditorContext& c, entt::entity e,
                                           int32_t slot,
                                           bool* compCreated, bool* slotCreated) {
    auto* m = c.registry->try_get<MaterialOverrideComponent>(e);
    if (!m) {
        m = &c.registry->emplace<MaterialOverrideComponent>(e);
        if (compCreated) *compCreated = true;
    }
    if (!m->slotOverrides.count(slot)) {
        m->slotOverrides.try_emplace(slot);
        if (slotCreated) *slotCreated = true;
    }
    return m;
}

void TeardownCreated(EditorContext& c, entt::entity e, int32_t slot,
                     bool compCreated, bool slotCreated) {
    auto* m = c.registry->try_get<MaterialOverrideComponent>(e);
    if (!m) return;
    if (slotCreated) m->slotOverrides.erase(slot);
    if (compCreated) c.registry->remove<MaterialOverrideComponent>(e);
}

} // anonymous namespace

bool DrawSlotOverrideEditor(entt::registry& reg, entt::entity entity,
                            Scene& scene, EditorContext& ctx,
                            int32_t slot, const MaterialType* effType)
{
    const MaterialManager* matMgr = ctx.matMgr;
    bool changed = false;

    auto* mat = reg.try_get<MaterialOverrideComponent>(entity);
    MaterialSlotOverride* ovr = nullptr;
    if (mat) {
        auto it = mat->slotOverrides.find(slot);
        if (it != mat->slotOverrides.end()) ovr = &it->second;
    }

    // ── Render-state inherit combos ───────────────────────────────────────────
    static const char* kSlotAlphaModes[]  = {"Inherit", "Opaque", "Mask", "Blend"};
    static const char* kSlotDoubleSided[] = {"Inherit", "Off", "On"};

    auto slotStateCombo = [&](const char* label, const char* comboId,
                              int8_t MaterialSlotOverride::* field,
                              const char* const* names, int count,
                              const char* undoName) {
        const int8_t curV = ovr ? ovr->*field : int8_t(-1);
        const int    cur  = static_cast<int>(curV) + 1;   // -1 → 0 (Inherit)
        int sel = cur;
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(std::max(60.f, ImGui::GetContentRegionAvail().x));
        if (ImGui::Combo(comboId, &sel, names, count) && sel != cur) {
            const int8_t oldV = curV;
            const int8_t newV = static_cast<int8_t>(sel - 1);
            if (ctx.cmdMgr) {
                auto compCreated = std::make_shared<bool>(false);
                auto slotCreated = std::make_shared<bool>(false);
                ctx.cmdMgr->Execute(std::make_unique<CallbackCommand>(undoName,
                    [entity, slot, field, newV, compCreated, slotCreated](EditorContext& c){
                        *compCreated = *slotCreated = false;
                        auto* m = EnsureSlotEntry(c, entity, slot,
                                                  compCreated.get(), slotCreated.get());
                        m->slotOverrides[slot].*field = newV;
                        c.scene->MarkMaterialDirty();
                    },
                    [entity, slot, field, oldV, compCreated, slotCreated](EditorContext& c){
                        if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) {
                            auto it = m->slotOverrides.find(slot);
                            if (it != m->slotOverrides.end()) it->second.*field = oldV;
                        }
                        TeardownCreated(c, entity, slot, *compCreated, *slotCreated);
                        c.scene->MarkMaterialDirty();
                    }),
                    ctx);
            } else {
                bool cc = false, sc = false;
                EnsureSlotEntry(ctx, entity, slot, &cc, &sc)
                    ->slotOverrides[slot].*field = newV;
            }
            changed = true;
        }
    };

    slotStateCombo("Alpha Mode",   "##sAlpha",
                   &MaterialSlotOverride::alphaMode,  kSlotAlphaModes,  4,
                   "Edit Slot Alpha Mode");
    slotStateCombo("Double Sided", "##sDS",
                   &MaterialSlotOverride::doubleSided, kSlotDoubleSided, 3,
                   "Edit Slot Double Sided");

    // ── Existing scalar overrides ─────────────────────────────────────────────
    if (ovr) {
        std::string rmScalar;
        for (auto& [pname, val] : ovr->scalars) {
            ImGui::PushID(pname.c_str());
            const ParamDef* def = FindParamDef(pname, matMgr);
            const char* lblStr  = (def && !def->displayName.empty())
                                  ? def->displayName.c_str() : pname.c_str();
            ImGui::TextUnformatted(lblStr);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(
                std::max(30.f, ImGui::GetContentRegionAvail().x - 28.f));
            ParamDef fallback;
            changed |= DrawReflectedParam(def ? *def : fallback, val, "##v",
                                          &ctx, "Edit Slot " + pname,
                                          [&scene]{ scene.MarkMaterialDirty(); });
            ImGui::SameLine();
            if (ImGui::SmallButton("-##rmS")) rmScalar = pname;
            ImGui::PopID();
        }
        if (!rmScalar.empty()) {
            const std::string key      = rmScalar;
            const ParamValue  savedVal = ovr->scalars[key];
            const int32_t     s        = slot;
            if (ctx.cmdMgr) {
                ctx.cmdMgr->Execute(std::make_unique<CallbackCommand>(
                    "Remove Slot Param",
                    [entity, s, key](EditorContext& c){ if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) { auto it = m->slotOverrides.find(s); if (it != m->slotOverrides.end()) { it->second.scalars.erase(key); c.scene->MarkMaterialDirty(); } } },
                    [entity, s, key, savedVal](EditorContext& c){ if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) { auto it = m->slotOverrides.find(s); if (it != m->slotOverrides.end()) { it->second.scalars[key] = savedVal; c.scene->MarkMaterialDirty(); } } }),
                    ctx);
            } else {
                ovr->scalars.erase(rmScalar);
            }
            changed = true;
        }
    }

    // ── Existing texture overrides ────────────────────────────────────────────
    if (ovr) {
        std::string rmTex;
        for (auto& [tname, tid] : ovr->textures) {
            ImGui::PushID(tname.c_str());
            const TextureDef* tdef   = FindTextureDef(tname, matMgr);
            const char*       lblStr = (tdef && !tdef->displayName.empty())
                                       ? tdef->displayName.c_str() : tname.c_str();
            if (ImGui::SmallButton("-##rmT")) { rmTex = tname; }
            else {
                ImGui::SameLine();
                if (DrawAssetIDField(lblStr, tid, "Texture", ctx.assetReg, ctx.iconCache))
                    changed = true;
            }
            ImGui::PopID();
        }
        if (!rmTex.empty()) {
            const std::string key      = rmTex;
            const AssetID     savedTex = ovr->textures[key];
            const int32_t     s        = slot;
            if (ctx.cmdMgr) {
                ctx.cmdMgr->Execute(std::make_unique<CallbackCommand>(
                    "Remove Slot Texture",
                    [entity, s, key](EditorContext& c){ if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) { auto it = m->slotOverrides.find(s); if (it != m->slotOverrides.end()) { it->second.textures.erase(key); c.scene->MarkMaterialDirty(); } } },
                    [entity, s, key, savedTex](EditorContext& c){ if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) { auto it = m->slotOverrides.find(s); if (it != m->slotOverrides.end()) { it->second.textures[key] = savedTex; c.scene->MarkMaterialDirty(); } } }),
                    ctx);
            } else {
                ovr->textures.erase(rmTex);
            }
            changed = true;
        }
    }

    // ── Add override (reflected from the slot's effective type) ───────────────
    if (ImGui::SmallButton("+ Add##slotAdd"))
        ImGui::OpenPopup("##slot_add");
    if (ImGui::BeginPopup("##slot_add")) {
        const int32_t s = slot;

        auto addSlotParam = [&](const ParamDef& param) {
            if (param.name.empty() || param.name[0] == '_') return;
            if (ovr && ovr->scalars.count(param.name)) return;
            const char* lbl = param.displayName.empty()
                              ? param.name.c_str() : param.displayName.c_str();
            if (ImGui::Selectable(lbl)) {
                const std::string key    = param.name;
                const ParamValue  defVal = DefaultParamValue(param);
                if (ctx.cmdMgr) {
                    auto compCreated = std::make_shared<bool>(false);
                    auto slotCreated = std::make_shared<bool>(false);
                    ctx.cmdMgr->Execute(std::make_unique<CallbackCommand>(
                        "Add Slot Param",
                        [entity, s, key, defVal, compCreated, slotCreated](EditorContext& c){
                            *compCreated = *slotCreated = false;
                            auto* m = EnsureSlotEntry(c, entity, s,
                                                      compCreated.get(), slotCreated.get());
                            m->slotOverrides[s].scalars[key] = defVal;
                            c.scene->MarkMaterialDirty();
                        },
                        [entity, s, key, compCreated, slotCreated](EditorContext& c){
                            if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) {
                                auto it = m->slotOverrides.find(s);
                                if (it != m->slotOverrides.end()) it->second.scalars.erase(key);
                            }
                            TeardownCreated(c, entity, s, *compCreated, *slotCreated);
                            c.scene->MarkMaterialDirty();
                        }),
                        ctx);
                } else {
                    bool cc = false, sc = false;
                    EnsureSlotEntry(ctx, entity, s, &cc, &sc)
                        ->slotOverrides[s].scalars[key] = defVal;
                }
                changed = true;
                ImGui::CloseCurrentPopup();
            }
        };
        auto addSlotTexture = [&](const TextureDef& tex) {
            if (ovr && ovr->textures.count(tex.name)) return;
            const char* lbl = tex.displayName.empty()
                              ? tex.name.c_str() : tex.displayName.c_str();
            if (ImGui::Selectable(lbl)) {
                const std::string key = tex.name;
                if (ctx.cmdMgr) {
                    auto compCreated = std::make_shared<bool>(false);
                    auto slotCreated = std::make_shared<bool>(false);
                    ctx.cmdMgr->Execute(std::make_unique<CallbackCommand>(
                        "Add Slot Texture",
                        [entity, s, key, compCreated, slotCreated](EditorContext& c){
                            *compCreated = *slotCreated = false;
                            auto* m = EnsureSlotEntry(c, entity, s,
                                                      compCreated.get(), slotCreated.get());
                            m->slotOverrides[s].textures[key] = AssetID::Invalid();
                            c.scene->MarkMaterialDirty();
                        },
                        [entity, s, key, compCreated, slotCreated](EditorContext& c){
                            if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) {
                                auto it = m->slotOverrides.find(s);
                                if (it != m->slotOverrides.end()) it->second.textures.erase(key);
                            }
                            TeardownCreated(c, entity, s, *compCreated, *slotCreated);
                            c.scene->MarkMaterialDirty();
                        }),
                        ctx);
                } else {
                    bool cc = false, sc = false;
                    EnsureSlotEntry(ctx, entity, s, &cc, &sc)
                        ->slotOverrides[s].textures[key] = AssetID::Invalid();
                }
                changed = true;
                ImGui::CloseCurrentPopup();
            }
        };

        ImGui::SeparatorText("Parameters");
        if (effType) {
            for (const auto& param : effType->params) addSlotParam(param);
        } else if (matMgr) {
            std::unordered_set<std::string> seen;
            for (const auto& [tn, tp] : matMgr->GetTypes())
                for (const auto& param : tp->params)
                    if (seen.insert(param.name).second) addSlotParam(param);
        }
        ImGui::SeparatorText("Textures");
        if (effType) {
            for (const auto& tex : effType->textures) addSlotTexture(tex);
        } else if (matMgr) {
            std::unordered_set<std::string> seen;
            for (const auto& [tn, tp] : matMgr->GetTypes())
                for (const auto& tex : tp->textures)
                    if (seen.insert(tex.name).second) addSlotTexture(tex);
        }
        ImGui::EndPopup();
    }

    // ── Clear this slot's overrides ───────────────────────────────────────────
    if (ovr) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear##slotClear")) {
            const int32_t s = slot;
            const MaterialSlotOverride saved = *ovr;
            if (ctx.cmdMgr) {
                ctx.cmdMgr->Execute(std::make_unique<CallbackCommand>(
                    "Clear Slot Overrides",
                    [entity, s](EditorContext& c){ if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) { m->slotOverrides.erase(s); c.scene->MarkMaterialDirty(); } },
                    [entity, s, saved](EditorContext& c){ if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) { m->slotOverrides[s] = saved; c.scene->MarkMaterialDirty(); } }),
                    ctx);
            } else {
                mat->slotOverrides.erase(s);
            }
            changed = true;
        }
    }

    return changed;
}

} // namespace StellarAlia::Editor
