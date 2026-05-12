#include "ui/drawers/MaterialOverrideDrawer.hpp"
#include "EditorContext.hpp"
#include "function/scene/Components.hpp"
#include "function/scene/Scene.hpp"
#include "function/material/MaterialManager.hpp"
#include "function/material/MaterialType.hpp"
#include "resource/AssetRegistry.hpp"
#include "ui/drawers/DrawerHelpers.hpp"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <fstream>
#include <string>
#include <unordered_set>
#include <variant>

namespace StellarAlia::Editor {

namespace {

std::string ReadMatTypeName(const std::filesystem::path& path) {
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        const auto pos = line.find("\"type\"");
        if (pos == std::string::npos) continue;
        const auto colon = line.find(':', pos);
        if (colon == std::string::npos) continue;
        const auto q1 = line.find('"', colon + 1);
        if (q1 == std::string::npos) continue;
        const auto q2 = line.find('"', q1 + 1);
        if (q2 == std::string::npos) continue;
        return line.substr(q1 + 1, q2 - q1 - 1);
    }
    return {};
}

const MaterialType* ResolveEffectiveType(const MaterialOverrideComponent* mat,
                                         const Resource::AssetRegistry* registry,
                                         const MaterialManager* matMgr) {
    if (!mat || !mat->materialAsset.IsValid() || !registry || !matMgr)
        return nullptr;
    const auto* entry = registry->FindByID(mat->materialAsset);
    if (!entry) return nullptr;
    const std::string typeName = ReadMatTypeName(entry->sourcePath);
    if (typeName.empty()) return nullptr;
    return matMgr->GetType(typeName);
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
    if (RemoveButton("x##rem_mo")) {
        reg.remove<MaterialOverrideComponent>(entity);
        scene.MarkMaterialDirty();
        return true;
    }
    if (!open) return true;

    const Resource::AssetRegistry* registry = ctx.assetReg;
    const MaterialManager*          matMgr  = ctx.matMgr;

    bool changed = false;
    ImGui::PushID("MatOvr");

    ImGui::PushID("matAsset");
    if (DrawAssetIDField("Material Asset", mat->materialAsset, "Material", registry))
        changed = true;
    ImGui::PopID();

    if (!mat->scalars.empty()) {
        ImGui::SeparatorText("Scalar Overrides");
        std::string toRemove;
        for (auto& [paramName, val] : mat->scalars) {
            ImGui::PushID(paramName.c_str());
            const ParamDef* def      = FindParamDef(paramName, matMgr);
            const char*     labelStr = (def && !def->displayName.empty())
                                       ? def->displayName.c_str() : paramName.c_str();

            using T = RHI::ParamUIType;
            const T uit = def ? def->uiType : T::Inferred;

            ImGui::TextUnformatted(labelStr);
            ImGui::SameLine();
            const float widgetW = std::max(30.f, ImGui::GetContentRegionAvail().x - 28.f);
            ImGui::SetNextItemWidth(widgetW);
            const std::string undoDesc = "Edit " + paramName;
            auto markDirty = [&scene]{ scene.MarkMaterialDirty(); };
            if (auto* f = std::get_if<float>(&val)) {
                const float lo  = def ? def->minValue : 0.f;
                const float hi  = def ? def->maxValue : 1.f;
                const float spd = (hi - lo) * 0.005f;
                changed |= TrackedFieldEdit(f, ctx, undoDesc,
                    [spd, lo, hi](float* p){ return ImGui::DragFloat("##v", p, spd, lo, hi); },
                    markDirty);
            } else if (auto* v2 = std::get_if<glm::vec2>(&val)) {
                changed |= TrackedFieldEdit(v2, ctx, undoDesc,
                    [](glm::vec2* p){ return ImGui::DragFloat2("##v", glm::value_ptr(*p), 0.01f); },
                    markDirty);
            } else if (auto* v3 = std::get_if<glm::vec3>(&val)) {
                if (uit == T::Color3 || uit == T::Inferred)
                    changed |= TrackedFieldEdit(v3, ctx, undoDesc,
                        [](glm::vec3* p){ return ImGui::ColorEdit3("##v", glm::value_ptr(*p)); },
                        markDirty);
                else
                    changed |= TrackedFieldEdit(v3, ctx, undoDesc,
                        [](glm::vec3* p){ return ImGui::DragFloat3("##v", glm::value_ptr(*p), 0.01f); },
                        markDirty);
            } else if (auto* v4 = std::get_if<glm::vec4>(&val)) {
                if (uit == T::Color4)
                    changed |= TrackedFieldEdit(v4, ctx, undoDesc,
                        [](glm::vec4* p){
                            return ImGui::ColorEdit4("##v", glm::value_ptr(*p),
                                                     ImGuiColorEditFlags_Float);
                        },
                        markDirty);
                else
                    changed |= TrackedFieldEdit(v4, ctx, undoDesc,
                        [](glm::vec4* p){ return ImGui::DragFloat4("##v", glm::value_ptr(*p), 0.01f); },
                        markDirty);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("-##rmS")) toRemove = paramName;
            ImGui::PopID();
        }
        if (!toRemove.empty()) { mat->scalars.erase(toRemove); changed = true; }
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
                if (DrawAssetIDField(labelStr, texID, "Texture", registry))
                    changed = true;
            }
            ImGui::PopID();
        }
        if (!toRemoveTex.empty()) { mat->textures.erase(toRemoveTex); changed = true; }
    }

    if (ImGui::SmallButton("+ Add Override"))
        ImGui::OpenPopup("##add_ovr");
    if (ImGui::BeginPopup("##add_ovr")) {
        if (matMgr) {
            const MaterialType* eff = ResolveEffectiveType(mat, registry, matMgr);

            auto addParamSelectable = [&](const ParamDef& param) {
                if (param.name.empty() || param.name[0] == '_') return;
                if (mat->scalars.count(param.name)) return;
                const char* lbl = param.displayName.empty()
                                  ? param.name.c_str()
                                  : param.displayName.c_str();
                if (ImGui::Selectable(lbl)) {
                    if (param.size == 16)
                        mat->scalars[param.name] = glm::vec4{
                            param.defaultValue[0], param.defaultValue[1],
                            param.defaultValue[2], param.defaultValue[3]};
                    else if (param.size == 12)
                        mat->scalars[param.name] = glm::vec3{
                            param.defaultValue[0], param.defaultValue[1],
                            param.defaultValue[2]};
                    else if (param.size == 8)
                        mat->scalars[param.name] = glm::vec2{
                            param.defaultValue[0], param.defaultValue[1]};
                    else
                        mat->scalars[param.name] = param.defaultValue[0];
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
                        mat->textures[tex.name] = AssetID::Invalid();
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
                            mat->textures[tex.name] = AssetID::Invalid();
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
