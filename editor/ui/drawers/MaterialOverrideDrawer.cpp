#include "ui/drawers/MaterialOverrideDrawer.hpp"
#include "EditorContext.hpp"
#include "function/scene/Components.hpp"
#include "function/scene/Scene.hpp"
#include "function/material/MaterialManager.hpp"
#include "function/material/MaterialType.hpp"
#include "resource/AssetRegistry.hpp"
#include "resource/ResourceManager.hpp"
#include "ui/drawers/DrawerHelpers.hpp"
#include "ui/drawers/ParamWidgets.hpp"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <string>
#include <unordered_set>
#include <variant>

namespace StellarAlia::Editor {

namespace {

// Cached load by UUID via VFS — no per-frame file I/O, and works for imported
// (.samatc-only) materials that have no AssetRegistry entry, unlike the old
// source-file text scan this replaces (Issue #101).
const MaterialType* ResolveEffectiveType(const MaterialOverrideComponent* mat,
                                         EditorContext& ctx) {
    if (!mat || !mat->materialAsset.IsValid() || !ctx.matMgr || !ctx.resMgr)
        return nullptr;
    MaterialInstance* inst = ctx.matMgr->LoadMaterial(mat->materialAsset, *ctx.resMgr);
    return inst ? inst->GetType() : nullptr;
}

const ParamDef* FindParamDef(const std::string& name, const MaterialManager* matMgr) {
    if (!matMgr) return nullptr;
    for (const auto& [tname, tptr] : matMgr->GetTypes())
        for (const auto& p : tptr->params)
            if (p.name == name) return &p;
    return nullptr;
}

const TextureDef* FindTextureDef(const std::string& name, const MaterialManager* matMgr) {
    if (!matMgr) return nullptr;
    for (const auto& [tname, tptr] : matMgr->GetTypes())
        for (const auto& t : tptr->textures)
            if (t.name == name) return &t;
    return nullptr;
}

} // anonymous namespace

bool MaterialOverrideDrawer::TryDraw(entt::registry& reg, entt::entity entity,
                                      Scene& scene, EditorContext& ctx) {
    auto* mat = reg.try_get<MaterialOverrideComponent>(entity);
    if (!mat) return false;
    bool open = ImGui::CollapsingHeader("Material Override",
                    HeaderFlags(ImGuiTreeNodeFlags_DefaultOpen));
    if (RemoveComponentButton<MaterialOverrideComponent>("x##rem_mo", reg, entity, ctx,
            "Remove Material Override", [&scene]{ scene.MarkMaterialDirty(); }))
        return true;
    if (!open) return true;

    const Resource::AssetRegistry* registry = ctx.assetReg;
    const MaterialManager*          matMgr  = ctx.matMgr;

    bool changed = false;
    ImGui::PushID("MatOvr");

    ImGui::PushID("matAsset");
    if (DrawAssetIDField("Material Asset", mat->materialAsset, "Material", registry))
        changed = true;
    ImGui::PopID();

    // ── Render state (Issue #56): pipeline-state overrides, -1 = inherit ─────
    {
        ImGui::SeparatorText("Render State");
        auto stateCombo = [&](const char* label, const char* comboId,
                              int8_t MaterialOverrideComponent::* field,
                              const char* const* names, int count,
                              const char* undoName) {
            const int cur = static_cast<int>(mat->*field) + 1;  // -1 → 0 (Inherit)
            int sel = cur;
            ImGui::TextUnformatted(label);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(std::max(60.f, ImGui::GetContentRegionAvail().x));
            if (ImGui::Combo(comboId, &sel, names, count) && sel != cur) {
                const int8_t oldV = mat->*field;
                const int8_t newV = static_cast<int8_t>(sel - 1);
                if (ctx.cmdMgr) {
                    ctx.cmdMgr->Execute(std::make_unique<CallbackCommand>(undoName,
                        [entity, field, newV](EditorContext& c){
                            if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) {
                                m->*field = newV; c.scene->MarkMaterialDirty();
                            }
                        },
                        [entity, field, oldV](EditorContext& c){
                            if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) {
                                m->*field = oldV; c.scene->MarkMaterialDirty();
                            }
                        }),
                        ctx);
                } else {
                    mat->*field = newV;
                }
                changed = true;
            }
        };
        static const char* kAlphaModes[]  = {"Inherit", "Opaque", "Mask", "Blend"};
        static const char* kDoubleSided[] = {"Inherit", "Off", "On"};
        stateCombo("Alpha Mode",   "##alphaMode",   &MaterialOverrideComponent::alphaMode,
                   kAlphaModes, 4, "Edit Alpha Mode");
        stateCombo("Double Sided", "##doubleSided", &MaterialOverrideComponent::doubleSided,
                   kDoubleSided, 3, "Edit Double Sided");
    }

    if (!mat->scalars.empty()) {
        ImGui::SeparatorText("Scalar Overrides");
        std::string toRemove;
        for (auto& [paramName, val] : mat->scalars) {
            ImGui::PushID(paramName.c_str());
            const ParamDef* def      = FindParamDef(paramName, matMgr);
            const char*     labelStr = (def && !def->displayName.empty())
                                       ? def->displayName.c_str() : paramName.c_str();

            ImGui::TextUnformatted(labelStr);
            ImGui::SameLine();
            const float widgetW = std::max(30.f, ImGui::GetContentRegionAvail().x - 28.f);
            ImGui::SetNextItemWidth(widgetW);
            const std::string undoDesc = "Edit " + paramName;
            ParamDef fallback;  // Inferred / [0,1] when the param has no live def
            changed |= DrawReflectedParam(def ? *def : fallback, val, "##v", &ctx, undoDesc,
                                          [&scene]{ scene.MarkMaterialDirty(); });
            ImGui::SameLine();
            if (ImGui::SmallButton("-##rmS")) toRemove = paramName;
            ImGui::PopID();
        }
        if (!toRemove.empty()) {
            if (ctx.cmdMgr) {
                const std::string key      = toRemove;
                const ParamValue  savedVal = mat->scalars[key];
                ctx.cmdMgr->Execute(std::make_unique<CallbackCommand>(
                    "Remove Material Param",
                    [entity, key](EditorContext& c){ if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) { m->scalars.erase(key); c.scene->MarkMaterialDirty(); } },
                    [entity, key, savedVal](EditorContext& c){ if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) { m->scalars[key] = savedVal; c.scene->MarkMaterialDirty(); } }),
                    ctx);
            } else {
                mat->scalars.erase(toRemove);
            }
            changed = true;
        }
    }

    if (!mat->textures.empty()) {
        ImGui::SeparatorText("Texture Overrides");
        std::string toRemoveTex;
        for (auto& [texName, texID] : mat->textures) {
            ImGui::PushID(texName.c_str());
            const TextureDef* tdef     = FindTextureDef(texName, matMgr);
            const char*       labelStr = (tdef && !tdef->displayName.empty())
                                         ? tdef->displayName.c_str() : texName.c_str();
            if (ImGui::SmallButton("-##rmT")) { toRemoveTex = texName; }
            else {
                ImGui::SameLine();
                if (DrawAssetIDField(labelStr, texID, "Texture", registry, ctx.iconCache))
                    changed = true;
            }
            ImGui::PopID();
        }
        if (!toRemoveTex.empty()) {
            if (ctx.cmdMgr) {
                const std::string key      = toRemoveTex;
                const AssetID     savedTex = mat->textures[key];
                ctx.cmdMgr->Execute(std::make_unique<CallbackCommand>(
                    "Remove Texture Override",
                    [entity, key](EditorContext& c){ if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) { m->textures.erase(key); c.scene->MarkMaterialDirty(); } },
                    [entity, key, savedTex](EditorContext& c){ if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) { m->textures[key] = savedTex; c.scene->MarkMaterialDirty(); } }),
                    ctx);
            } else {
                mat->textures.erase(toRemoveTex);
            }
            changed = true;
        }
    }

    // ── Per-submesh overrides (Issue #101) ───────────────────────────────────
    {
        ImGui::SeparatorText("Slot Overrides");

        // Resolve the mesh for slot count + material-name labels (v6 .samesh).
        const Resource::GPUMesh* gpuMesh = nullptr;
        if (ctx.resMgr) {
            AssetID meshId;
            if (const auto* smc = reg.try_get<StaticMeshComponent>(entity))
                meshId = smc->meshAsset;
            else if (const auto* skc = reg.try_get<SkinnedMeshComponent>(entity))
                meshId = skc->meshAsset;
            if (meshId.IsValid()) gpuMesh = ctx.resMgr->LoadMesh(meshId);
        }
        auto slotLabel = [&](int32_t slot) {
            char buf[96];
            const char* nm =
                (gpuMesh && slot >= 0 &&
                 slot < static_cast<int32_t>(gpuMesh->subMeshes.size()) &&
                 !gpuMesh->subMeshes[slot].materialName.empty())
                ? gpuMesh->subMeshes[slot].materialName.c_str() : "";
            if (nm[0]) std::snprintf(buf, sizeof(buf), "Slot [%d] %s", slot, nm);
            else       std::snprintf(buf, sizeof(buf), "Slot [%d]", slot);
            return std::string(buf);
        };

        static const char* kSlotAlphaModes[]  = {"Inherit", "Opaque", "Mask", "Blend"};
        static const char* kSlotDoubleSided[] = {"Inherit", "Off", "On"};

        auto slotStateCombo = [&](const char* label, const char* comboId, int32_t slot,
                                  int8_t MaterialSlotOverride::* field,
                                  const char* const* names, int count,
                                  const char* undoName) {
            auto& ovr = mat->slotOverrides[slot];
            const int cur = static_cast<int>(ovr.*field) + 1;   // -1 → 0 (Inherit)
            int sel = cur;
            ImGui::TextUnformatted(label);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(std::max(60.f, ImGui::GetContentRegionAvail().x));
            if (ImGui::Combo(comboId, &sel, names, count) && sel != cur) {
                const int8_t oldV = ovr.*field;
                const int8_t newV = static_cast<int8_t>(sel - 1);
                auto setField = [entity, slot, field](EditorContext& c, int8_t v) {
                    if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) {
                        auto it = m->slotOverrides.find(slot);
                        if (it != m->slotOverrides.end()) {
                            it->second.*field = v;
                            c.scene->MarkMaterialDirty();
                        }
                    }
                };
                if (ctx.cmdMgr) {
                    ctx.cmdMgr->Execute(std::make_unique<CallbackCommand>(undoName,
                        [setField, newV](EditorContext& c){ setField(c, newV); },
                        [setField, oldV](EditorContext& c){ setField(c, oldV); }),
                        ctx);
                } else {
                    ovr.*field = newV;
                }
                changed = true;
            }
        };

        int32_t slotToRemove = 0;
        bool    hasRemove    = false;

        for (auto& [slot, ovr] : mat->slotOverrides) {
            ImGui::PushID(slot);
            const bool openNode =
                ImGui::TreeNode("slot_ovr", "%s", slotLabel(slot).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("x##rmSlot")) { slotToRemove = slot; hasRemove = true; }
            if (openNode) {
                slotStateCombo("Alpha Mode",   "##sAlpha", slot,
                               &MaterialSlotOverride::alphaMode,
                               kSlotAlphaModes, 4, "Edit Slot Alpha Mode");
                slotStateCombo("Double Sided", "##sDS",    slot,
                               &MaterialSlotOverride::doubleSided,
                               kSlotDoubleSided, 3, "Edit Slot Double Sided");

                std::string rmScalar;
                for (auto& [pname, val] : ovr.scalars) {
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
                    const ParamValue  savedVal = ovr.scalars[key];
                    const int32_t     s        = slot;
                    if (ctx.cmdMgr) {
                        ctx.cmdMgr->Execute(std::make_unique<CallbackCommand>(
                            "Remove Slot Param",
                            [entity, s, key](EditorContext& c){ if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) { auto it = m->slotOverrides.find(s); if (it != m->slotOverrides.end()) { it->second.scalars.erase(key); c.scene->MarkMaterialDirty(); } } },
                            [entity, s, key, savedVal](EditorContext& c){ if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) { auto it = m->slotOverrides.find(s); if (it != m->slotOverrides.end()) { it->second.scalars[key] = savedVal; c.scene->MarkMaterialDirty(); } } }),
                            ctx);
                    } else {
                        ovr.scalars.erase(rmScalar);
                    }
                    changed = true;
                }

                std::string rmTex;
                for (auto& [tname, tid] : ovr.textures) {
                    ImGui::PushID(tname.c_str());
                    const TextureDef* tdef   = FindTextureDef(tname, matMgr);
                    const char*       lblStr = (tdef && !tdef->displayName.empty())
                                               ? tdef->displayName.c_str() : tname.c_str();
                    if (ImGui::SmallButton("-##rmT")) { rmTex = tname; }
                    else {
                        ImGui::SameLine();
                        if (DrawAssetIDField(lblStr, tid, "Texture", registry, ctx.iconCache))
                            changed = true;
                    }
                    ImGui::PopID();
                }
                if (!rmTex.empty()) {
                    const std::string key      = rmTex;
                    const AssetID     savedTex = ovr.textures[key];
                    const int32_t     s        = slot;
                    if (ctx.cmdMgr) {
                        ctx.cmdMgr->Execute(std::make_unique<CallbackCommand>(
                            "Remove Slot Texture",
                            [entity, s, key](EditorContext& c){ if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) { auto it = m->slotOverrides.find(s); if (it != m->slotOverrides.end()) { it->second.textures.erase(key); c.scene->MarkMaterialDirty(); } } },
                            [entity, s, key, savedTex](EditorContext& c){ if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) { auto it = m->slotOverrides.find(s); if (it != m->slotOverrides.end()) { it->second.textures[key] = savedTex; c.scene->MarkMaterialDirty(); } } }),
                            ctx);
                    } else {
                        ovr.textures.erase(rmTex);
                    }
                    changed = true;
                }

                if (ImGui::SmallButton("+ Add##slotAdd"))
                    ImGui::OpenPopup("##slot_add");
                if (ImGui::BeginPopup("##slot_add")) {
                    const MaterialType* eff = ResolveEffectiveType(mat, ctx);
                    const int32_t s = slot;

                    auto addSlotParam = [&](const ParamDef& param) {
                        if (param.name.empty() || param.name[0] == '_') return;
                        if (ovr.scalars.count(param.name)) return;
                        const char* lbl = param.displayName.empty()
                                          ? param.name.c_str() : param.displayName.c_str();
                        if (ImGui::Selectable(lbl)) {
                            const std::string key    = param.name;
                            const ParamValue  defVal = DefaultParamValue(param);
                            if (ctx.cmdMgr) {
                                ctx.cmdMgr->Execute(std::make_unique<CallbackCommand>(
                                    "Add Slot Param",
                                    [entity, s, key, defVal](EditorContext& c){ if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) { m->slotOverrides[s].scalars[key] = defVal; c.scene->MarkMaterialDirty(); } },
                                    [entity, s, key](EditorContext& c){ if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) { auto it = m->slotOverrides.find(s); if (it != m->slotOverrides.end()) { it->second.scalars.erase(key); c.scene->MarkMaterialDirty(); } } }),
                                    ctx);
                            } else {
                                ovr.scalars[key] = defVal;
                            }
                            changed = true;
                            ImGui::CloseCurrentPopup();
                        }
                    };
                    auto addSlotTexture = [&](const TextureDef& tex) {
                        if (ovr.textures.count(tex.name)) return;
                        const char* lbl = tex.displayName.empty()
                                          ? tex.name.c_str() : tex.displayName.c_str();
                        if (ImGui::Selectable(lbl)) {
                            const std::string key = tex.name;
                            if (ctx.cmdMgr) {
                                ctx.cmdMgr->Execute(std::make_unique<CallbackCommand>(
                                    "Add Slot Texture",
                                    [entity, s, key](EditorContext& c){ if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) { m->slotOverrides[s].textures[key] = AssetID::Invalid(); c.scene->MarkMaterialDirty(); } },
                                    [entity, s, key](EditorContext& c){ if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) { auto it = m->slotOverrides.find(s); if (it != m->slotOverrides.end()) { it->second.textures.erase(key); c.scene->MarkMaterialDirty(); } } }),
                                    ctx);
                            } else {
                                ovr.textures[key] = AssetID::Invalid();
                            }
                            changed = true;
                            ImGui::CloseCurrentPopup();
                        }
                    };

                    ImGui::SeparatorText("Parameters");
                    if (eff) {
                        for (const auto& param : eff->params) addSlotParam(param);
                    } else if (matMgr) {
                        std::unordered_set<std::string> seen;
                        for (const auto& [tn, tp] : matMgr->GetTypes())
                            for (const auto& param : tp->params)
                                if (seen.insert(param.name).second) addSlotParam(param);
                    }
                    ImGui::SeparatorText("Textures");
                    if (eff) {
                        for (const auto& tex : eff->textures) addSlotTexture(tex);
                    } else if (matMgr) {
                        std::unordered_set<std::string> seen;
                        for (const auto& [tn, tp] : matMgr->GetTypes())
                            for (const auto& tex : tp->textures)
                                if (seen.insert(tex.name).second) addSlotTexture(tex);
                    }
                    ImGui::EndPopup();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }

        if (hasRemove) {
            const int32_t s = slotToRemove;
            const MaterialSlotOverride saved = mat->slotOverrides[s];
            if (ctx.cmdMgr) {
                ctx.cmdMgr->Execute(std::make_unique<CallbackCommand>(
                    "Remove Slot Override",
                    [entity, s](EditorContext& c){ if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) { m->slotOverrides.erase(s); c.scene->MarkMaterialDirty(); } },
                    [entity, s, saved](EditorContext& c){ if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) { m->slotOverrides[s] = saved; c.scene->MarkMaterialDirty(); } }),
                    ctx);
            } else {
                mat->slotOverrides.erase(s);
            }
            changed = true;
        }

        if (ImGui::SmallButton("+ Add Slot Override"))
            ImGui::OpenPopup("##add_slot_ovr");
        if (ImGui::BeginPopup("##add_slot_ovr")) {
            const int32_t slotCount =
                gpuMesh ? static_cast<int32_t>(gpuMesh->subMeshes.size()) : 0;
            if (slotCount == 0)
                ImGui::TextDisabled("(no mesh / submeshes)");
            for (int32_t si = 0; si < slotCount; ++si) {
                if (mat->slotOverrides.count(si)) continue;
                if (ImGui::Selectable(slotLabel(si).c_str())) {
                    if (ctx.cmdMgr) {
                        ctx.cmdMgr->Execute(std::make_unique<CallbackCommand>(
                            "Add Slot Override",
                            [entity, si](EditorContext& c){ if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) { m->slotOverrides.try_emplace(si); c.scene->MarkMaterialDirty(); } },
                            [entity, si](EditorContext& c){ if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) { m->slotOverrides.erase(si); c.scene->MarkMaterialDirty(); } }),
                            ctx);
                    } else {
                        mat->slotOverrides.try_emplace(si);
                    }
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
    }

    if (ImGui::SmallButton("+ Add Override"))
        ImGui::OpenPopup("##add_ovr");
    if (ImGui::BeginPopup("##add_ovr")) {
        if (matMgr) {
            const MaterialType* eff = ResolveEffectiveType(mat, ctx);

            auto addParamSelectable = [&](const ParamDef& param) {
                if (param.name.empty() || param.name[0] == '_') return;
                if (mat->scalars.count(param.name)) return;
                const char* lbl = param.displayName.empty()
                                  ? param.name.c_str()
                                  : param.displayName.c_str();
                if (ImGui::Selectable(lbl)) {
                    const std::string key    = param.name;
                    const ParamValue  defVal = DefaultParamValue(param);
                    if (ctx.cmdMgr) {
                        ctx.cmdMgr->Execute(std::make_unique<CallbackCommand>(
                            "Add Material Param",
                            [entity, key, defVal](EditorContext& c){ if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) { m->scalars[key] = defVal; c.scene->MarkMaterialDirty(); } },
                            [entity, key](EditorContext& c){ if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) { m->scalars.erase(key); c.scene->MarkMaterialDirty(); } }),
                            ctx);
                    } else {
                        mat->scalars[key] = defVal;
                    }
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
            };

            ImGui::SeparatorText("Parameters");
            if (eff) {
                for (const auto& param : eff->params)
                    addParamSelectable(param);
            } else {
                std::unordered_set<std::string> seen;
                for (const auto& [typeName, typePtr] : matMgr->GetTypes())
                    for (const auto& param : typePtr->params)
                        if (seen.insert(param.name).second)
                            addParamSelectable(param);
            }

            ImGui::SeparatorText("Textures");
            if (eff) {
                for (const auto& tex : eff->textures) {
                    if (mat->textures.count(tex.name)) continue;
                    const char* lbl = tex.displayName.empty()
                                      ? tex.name.c_str()
                                      : tex.displayName.c_str();
                    if (ImGui::Selectable(lbl)) {
                        const std::string key = tex.name;
                        if (ctx.cmdMgr) {
                            ctx.cmdMgr->Execute(std::make_unique<CallbackCommand>(
                                "Add Texture Override",
                                [entity, key](EditorContext& c){ if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) { m->textures[key] = AssetID::Invalid(); c.scene->MarkMaterialDirty(); } },
                                [entity, key](EditorContext& c){ if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) { m->textures.erase(key); c.scene->MarkMaterialDirty(); } }),
                                ctx);
                        } else {
                            mat->textures[key] = AssetID::Invalid();
                        }
                        changed = true;
                        ImGui::CloseCurrentPopup();
                    }
                }
            } else {
                std::unordered_set<std::string> seenTex;
                for (const auto& [typeName, typePtr] : matMgr->GetTypes()) {
                    for (const auto& tex : typePtr->textures) {
                        if (!seenTex.insert(tex.name).second) continue;
                        if (mat->textures.count(tex.name)) continue;
                        const char* lbl = tex.displayName.empty()
                                          ? tex.name.c_str()
                                          : tex.displayName.c_str();
                        if (ImGui::Selectable(lbl)) {
                            const std::string key = tex.name;
                            if (ctx.cmdMgr) {
                                ctx.cmdMgr->Execute(std::make_unique<CallbackCommand>(
                                    "Add Texture Override",
                                    [entity, key](EditorContext& c){ if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) { m->textures[key] = AssetID::Invalid(); c.scene->MarkMaterialDirty(); } },
                                    [entity, key](EditorContext& c){ if (auto* m = c.registry->try_get<MaterialOverrideComponent>(entity)) { m->textures.erase(key); c.scene->MarkMaterialDirty(); } }),
                                    ctx);
                            } else {
                                mat->textures[key] = AssetID::Invalid();
                            }
                            changed = true;
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
            }
        }
        ImGui::EndPopup();
    }

    ImGui::PopID();
    if (changed) scene.MarkMaterialDirty();
    return true;
}

} // namespace StellarAlia::Editor
