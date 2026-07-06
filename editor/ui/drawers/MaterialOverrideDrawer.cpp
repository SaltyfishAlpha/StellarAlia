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
    // #106: shared material field — invalid = inherit per-slot / mesh default.
    if (DrawMaterialField("Material Asset", mat->materialAsset,
                          "per-slot / mesh default", registry))
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
