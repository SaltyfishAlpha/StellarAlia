#pragma once

#include "ui/IComponentDrawer.hpp"
#include "function/material/MaterialManager.hpp"
#include "function/material/MaterialType.hpp"
#include "function/scene/Scene.hpp"
#include "function/scene/Components.hpp"
#include "resource/AssetRegistry.hpp"

#include <imgui.h>
#include "ui/IconsFontAwesome6.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdio>
#include <fstream>
#include <string>
#include <unordered_set>
#include <variant>

namespace StellarAlia::Editor {

// ── Helpers ────────────────────────────────────────────────────────────────────

// Read-only fallback: just show the UUID prefix as a label.
inline void DrawAssetID(const char* label, const AssetID& id) {
    if (id.IsValid()) {
        std::string s = id.ToString();
        ImGui::LabelText(label, "%.8s…", s.c_str());
    } else {
        ImGui::LabelText(label, "(none)");
    }
}

// Interactive AssetID picker.
// Shows a button (asset name, or "none") that opens a popup with a filterable
// list of registered assets.  Returns true if the id was changed.
// filterType — "Mesh", "Texture", etc.; nullptr or "" = show all.
// registry   — may be nullptr; falls back to read-only DrawAssetID.
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

    // ── Button label ──────────────────────────────────────────────────────────
    const char* btnLabel = "(none)";
    std::string nameStorage;
    if (id.IsValid()) {
        if (const auto* e = registry->FindByID(id)) {
            nameStorage = e->name;
            btnLabel    = nameStorage.c_str();
        } else {
            nameStorage = id.ToString().substr(0, 8) + "…";
            btnLabel    = nameStorage.c_str();
        }
    }

    // Label first, then picker button fills remaining width.
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    const float clearW   = id.IsValid() ? 26.f : 0.f;
    const float btnWidth = std::max(10.f, ImGui::GetContentRegionAvail().x - clearW);
    if (ImGui::Button(btnLabel, ImVec2(btnWidth, 0)))
        ImGui::OpenPopup("##asset_pick");

    // Clear button — only shown when an asset is set.
    if (id.IsValid()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("×")) {
            id      = AssetID::Invalid();
            changed = true;
        }
    }

    // ── Picker popup ─────────────────────────────────────────────────────────
    if (ImGui::BeginPopup("##asset_pick")) {
        static char filter[128] = {};
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##flt", "Filter…", filter, sizeof(filter));

        ImGui::Separator();
        ImGui::BeginChild("##list", ImVec2(280, 200), false);

        const std::string_view ft = filterType ? filterType : "";
        for (const auto* e : registry->EntriesByType(ft)) {
            // Apply the filter string.
            if (filter[0] != '\0') {
                // Simple case-insensitive substring search.
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

// Places a small "×" button at the right edge of the window.
// Call immediately after CollapsingHeader() — returns true when clicked.
// Use with CollapsingHeader(..., ImGuiTreeNodeFlags_AllowOverlap) so the
// header does not capture the click before the button can receive it.
inline bool RemoveButton(const char* id) {
    float btnX = ImGui::GetWindowWidth() - 28.f;
    ImGui::SameLine(btnX > 0.f ? btnX : 0.f);
    return ImGui::SmallButton(id);
}

// Returns ImGuiTreeNodeFlags with AllowOverlap always set, merged with any
// extra flags supplied by the caller.
inline ImGuiTreeNodeFlags HeaderFlags(ImGuiTreeNodeFlags extra = 0) {
    return ImGuiTreeNodeFlags_AllowOverlap | extra;
}

// ── TagDrawer ──────────────────────────────────────────────────────────────────
class TagDrawer : public IComponentDrawer {
public:
    bool TryDraw(entt::registry& reg, entt::entity entity, Scene& /*scene*/) override {
        auto* tag = reg.try_get<TagComponent>(entity);
        if (!tag) return false;
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s", tag->name.c_str());
        if (ImGui::InputText("Name", buf, sizeof(buf)))
            tag->name = buf;
        ImGui::Separator();
        return true;
    }
};

// ── TransformDrawer ────────────────────────────────────────────────────────────
class TransformDrawer : public IComponentDrawer {
public:
    bool TryDraw(entt::registry& reg, entt::entity entity, Scene& scene) override {
        auto* tr = reg.try_get<TransformComponent>(entity);
        if (!tr) return false;
        if (!ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
            return true;

        bool changed = false;
        changed |= ImGui::DragFloat3("Position", glm::value_ptr(tr->position), 0.1f);

        // Re-seed cached Euler only when selection changes — avoids quat→euler
        // round-trip each frame which clamps Y to [-90,+90] via asin.
        const uint32_t sel = static_cast<uint32_t>(entity);
        if (sel != m_cachedEulerEntity) {
            m_cachedEuler       = glm::degrees(glm::eulerAngles(tr->rotation));
            m_cachedEulerEntity = sel;
        }
        if (ImGui::DragFloat3("Rotation (deg)", glm::value_ptr(m_cachedEuler), 0.5f)) {
            tr->rotation = glm::quat(glm::radians(m_cachedEuler));
            changed = true;
        }
        // ── Scale ──────────────────────────────────────────────────────────────
        {
            const float btnSz = ImGui::GetFrameHeight();

            ImGui::PushID("##scale_lock");
            const bool wasLocked = m_scaleLocked;
            if (wasLocked)
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.90f, 1.f));
            const ImVec2 btnPos = ImGui::GetCursorScreenPos();
            if (ImGui::Button("##btn", ImVec2(btnSz, btnSz)))
                m_scaleLocked = !m_scaleLocked;
            if (wasLocked)
                ImGui::PopStyleColor();
            if (m_iconFont) {
                const char*  icon   = wasLocked ? ICON_FA_LINK : ICON_FA_LINK_SLASH;
                const float  iconPx = btnSz - 2.f * ImGui::GetStyle().FramePadding.y;
                const ImVec2 tsz    = m_iconFont->CalcTextSizeA(iconPx, FLT_MAX, 0.f, icon);
                ImGui::GetWindowDrawList()->AddText(
                    m_iconFont, iconPx,
                    ImVec2(btnPos.x + (btnSz - tsz.x) * 0.5f,
                           btnPos.y + (btnSz - tsz.y) * 0.5f),
                    IM_COL32_WHITE, icon);
            }
            ImGui::PopID();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", m_scaleLocked
                    ? "Scale: proportional  (click to unlock)"
                    : "Scale: independent   (click to lock)");

            ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::SetNextItemWidth(ImGui::CalcItemWidth()
                                    - btnSz - ImGui::GetStyle().ItemInnerSpacing.x);

            const glm::vec3 prev = tr->scale;
            if (ImGui::DragFloat3("Scale", glm::value_ptr(tr->scale), 0.01f, 0.001f, 1000.f)) {
                if (m_scaleLocked) {
                    const glm::vec3 d = tr->scale - prev;
                    int axis = (std::abs(d[1]) > std::abs(d[0])) ? 1 : 0;
                    if (std::abs(d[2]) > std::abs(d[axis])) axis = 2;
                    const float ratio = std::abs(prev[axis]) > 1e-6f
                                      ? tr->scale[axis] / prev[axis] : 1.f;
                    tr->scale = prev * ratio;
                }
                changed = true;
            }
        }
        if (changed) scene.MarkDirty(entity);
        return true;
    }

private:
    uint32_t  m_cachedEulerEntity = ~0u;
    glm::vec3 m_cachedEuler       = {};
    bool      m_scaleLocked       = true;
};

// ── CameraDrawer ───────────────────────────────────────────────────────────────
class CameraDrawer : public IComponentDrawer {
public:
    bool TryDraw(entt::registry& reg, entt::entity entity, Scene& /*scene*/) override {
        auto* cam = reg.try_get<CameraComponent>(entity);
        if (!cam) return false;
        bool open = ImGui::CollapsingHeader("Camera", HeaderFlags());
        if (RemoveButton("x##rem_cam")) { reg.remove<CameraComponent>(entity); return true; }
        if (!open) return true;
        float fovDeg = glm::degrees(cam->fovY);
        if (ImGui::DragFloat("FoV Y (deg)", &fovDeg, 0.5f, 10.f, 170.f))
            cam->fovY = glm::radians(fovDeg);
        ImGui::DragFloat("Near",     &cam->nearPlane, 0.001f, 0.001f,  10.f);
        ImGui::DragFloat("Far",      &cam->farPlane,  1.f,    1.f,    10000.f);
        ImGui::DragInt  ("Priority", &cam->priority,  1.f,   -100,    100);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Highest priority camera is used for the primary view.");
        return true;
    }
};

// ── DirectionalLightDrawer ─────────────────────────────────────────────────────
class DirectionalLightDrawer : public IComponentDrawer {
public:
    bool TryDraw(entt::registry& reg, entt::entity entity, Scene& /*scene*/) override {
        auto* dl = reg.try_get<DirectionalLightComponent>(entity);
        if (!dl) return false;
        bool open = ImGui::CollapsingHeader("Directional Light", HeaderFlags());
        if (RemoveButton("x##rem_dl")) { reg.remove<DirectionalLightComponent>(entity); return true; }
        if (!open) return true;
        ImGui::ColorEdit3("Color",       glm::value_ptr(dl->color));
        ImGui::DragFloat("Intensity",   &dl->intensity, 0.05f, 0.f, 100.f);
        ImGui::Checkbox("Cast Shadow",  &dl->castShadow);
        return true;
    }
};

// ── PointLightDrawer ───────────────────────────────────────────────────────────
class PointLightDrawer : public IComponentDrawer {
public:
    bool TryDraw(entt::registry& reg, entt::entity entity, Scene& /*scene*/) override {
        auto* pl = reg.try_get<PointLightComponent>(entity);
        if (!pl) return false;
        bool open = ImGui::CollapsingHeader("Point Light", HeaderFlags());
        if (RemoveButton("x##rem_pl")) { reg.remove<PointLightComponent>(entity); return true; }
        if (!open) return true;
        ImGui::ColorEdit3("Color",     glm::value_ptr(pl->color));
        ImGui::DragFloat("Intensity", &pl->intensity, 0.05f, 0.f, 1000.f);
        ImGui::DragFloat("Range",     &pl->range,     0.1f,  0.f,  500.f);
        return true;
    }
};

// ── SpotLightDrawer ────────────────────────────────────────────────────────────
class SpotLightDrawer : public IComponentDrawer {
public:
    bool TryDraw(entt::registry& reg, entt::entity entity, Scene& /*scene*/) override {
        auto* sl = reg.try_get<SpotLightComponent>(entity);
        if (!sl) return false;
        bool open = ImGui::CollapsingHeader("Spot Light", HeaderFlags());
        if (RemoveButton("x##rem_sl")) { reg.remove<SpotLightComponent>(entity); return true; }
        if (!open) return true;
        ImGui::ColorEdit3("Color",     glm::value_ptr(sl->color));
        ImGui::DragFloat("Intensity", &sl->intensity, 0.05f,  0.f, 1000.f);
        ImGui::DragFloat("Range",     &sl->range,     0.1f,   0.f,  500.f);
        float innerDeg = glm::degrees(sl->innerAngle);
        float outerDeg = glm::degrees(sl->outerAngle);
        if (ImGui::DragFloat("Inner Angle (deg)", &innerDeg, 0.5f, 0.f, 89.f))
            sl->innerAngle = glm::radians(innerDeg);
        if (ImGui::DragFloat("Outer Angle (deg)", &outerDeg, 0.5f, 0.f, 89.f))
            sl->outerAngle = glm::radians(outerDeg);
        return true;
    }
};

// ── AreaLightDrawer ────────────────────────────────────────────────────────────
class AreaLightDrawer : public IComponentDrawer {
public:
    bool TryDraw(entt::registry& reg, entt::entity entity, Scene& /*scene*/) override {
        auto* al = reg.try_get<AreaLightComponent>(entity);
        if (!al) return false;
        bool open = ImGui::CollapsingHeader("Area Light", HeaderFlags());
        if (RemoveButton("x##rem_al")) { reg.remove<AreaLightComponent>(entity); return true; }
        if (!open) return true;
        ImGui::ColorEdit3("Color",         glm::value_ptr(al->color));
        ImGui::DragFloat("Intensity",      &al->intensity,     5.f,   0.f, 10000.f);
        ImGui::DragFloat2("Size (m)",      glm::value_ptr(al->size), 0.01f, 0.01f, 100.f);
        ImGui::Checkbox("Two-Sided",       &al->twoSided);
        ImGui::DragFloat("Emissive Scale", &al->emissiveScale, 0.05f, 0.f,   10.f);
        return true;
    }
};

// ── StaticMeshDrawer ───────────────────────────────────────────────────────────
class StaticMeshDrawer : public IComponentDrawer {
public:
    explicit StaticMeshDrawer(const Resource::AssetRegistry* reg = nullptr)
        : m_registry(reg) {}

    bool TryDraw(entt::registry& reg, entt::entity entity, Scene& scene) override {
        auto* sm = reg.try_get<StaticMeshComponent>(entity);
        if (!sm) return false;
        bool open = ImGui::CollapsingHeader("Static Mesh", HeaderFlags());
        if (RemoveButton("x##rem_sm")) {
            reg.remove<StaticMeshComponent>(entity);
            scene.MarkMaterialDirty();
            return true;
        }
        if (!open) return true;

        ImGui::PushID("StaticMesh");
        bool changed = DrawAssetIDField("Mesh Asset", sm->meshAsset, "Mesh", m_registry);
        ImGui::PopID();
        if (changed) scene.MarkMaterialDirty();
        return true;
    }

private:
    const Resource::AssetRegistry* m_registry = nullptr;
};


// ── AnimatorDrawer ─────────────────────────────────────────────────────────────
class AnimatorDrawer : public IComponentDrawer {
public:
    explicit AnimatorDrawer(const Resource::AssetRegistry* reg = nullptr)
        : m_registry(reg) {}

    bool TryDraw(entt::registry& reg, entt::entity entity, Scene& /*scene*/) override {
        auto* anim = reg.try_get<AnimatorComponent>(entity);
        if (!anim) return false;
        bool open = ImGui::CollapsingHeader("Animator", HeaderFlags(ImGuiTreeNodeFlags_DefaultOpen));
        if (RemoveButton("x##rem_anim")) { reg.remove<AnimatorComponent>(entity); return true; }
        if (!open) return true;
        DrawAssetIDField("Clip Asset", anim->clipAsset, "Animation", m_registry);
        ImGui::DragFloat("Time (s)", &anim->time,  0.01f, 0.f, 3600.f);
        ImGui::DragFloat("Speed",    &anim->speed, 0.01f, 0.f,   10.f);
        ImGui::Checkbox("Looping",  &anim->looping);
        ImGui::SameLine();
        ImGui::Checkbox("Playing",  &anim->playing);
        return true;
    }

private:
    const Resource::AssetRegistry* m_registry = nullptr;
};

// ── SkinnedMeshDrawer ──────────────────────────────────────────────────────────
class SkinnedMeshDrawer : public IComponentDrawer {
public:
    explicit SkinnedMeshDrawer(const Resource::AssetRegistry* reg = nullptr)
        : m_registry(reg) {}

    bool TryDraw(entt::registry& reg, entt::entity entity, Scene& scene) override {
        auto* sm = reg.try_get<SkinnedMeshComponent>(entity);
        if (!sm) return false;
        bool open = ImGui::CollapsingHeader("Skinned Mesh", HeaderFlags());
        if (RemoveButton("x##rem_sk")) { reg.remove<SkinnedMeshComponent>(entity); return true; }
        if (!open) return true;

        ImGui::PushID("SkinnedMesh");
        // When mesh changes, clear runtime state and signal Application to re-prepare.
        if (DrawAssetIDField("Mesh Asset", sm->meshAsset, "Mesh", m_registry)) {
            sm->ready = false;
            scene.MarkSkinnedMeshDirty();
        }
        ImGui::LabelText("Bones",  "%u", sm->boneCount);
        ImGui::LabelText("Status", "%s", sm->ready ? "Ready" : "Pending");
        ImGui::PopID();
        return true;
    }

private:
    const Resource::AssetRegistry* m_registry = nullptr;
};

// ── MeshRendererDrawer ─────────────────────────────────────────────────────────
class MeshRendererDrawer : public IComponentDrawer {
public:
    explicit MeshRendererDrawer(const Resource::AssetRegistry* reg = nullptr)
        : m_registry(reg) {}

    bool TryDraw(entt::registry& reg, entt::entity entity, Scene& scene) override {
        auto* mr = reg.try_get<MeshRendererComponent>(entity);
        if (!mr) return false;
        bool open = ImGui::CollapsingHeader("Mesh Renderer", HeaderFlags(ImGuiTreeNodeFlags_DefaultOpen));
        if (RemoveButton("x##rem_mr")) {
            reg.remove<MeshRendererComponent>(entity);
            scene.MarkMaterialDirty();
            return true;
        }
        if (!open) return true;

        ImGui::PushID("MeshRenderer");
        bool changed = false;

        ImGui::Checkbox("Cast Shadow",    &mr->castShadow);
        ImGui::SameLine();
        ImGui::Checkbox("Receive Shadow", &mr->receiveShadow);

        char slotLabel[64];
        if (mr->materialSlots.empty())
            std::snprintf(slotLabel, sizeof(slotLabel), "Material Slots (0, using mesh defaults)");
        else
            std::snprintf(slotLabel, sizeof(slotLabel), "Material Slots (%zu)", mr->materialSlots.size());

        if (ImGui::TreeNode("matslots_mr", "%s", slotLabel)) {
            for (size_t i = 0; i < mr->materialSlots.size(); ++i) {
                char lbl[16];
                std::snprintf(lbl, sizeof(lbl), "[%zu]", i);
                changed |= DrawAssetIDField(lbl, mr->materialSlots[i], "Material", m_registry);
            }
            if (ImGui::SmallButton("+ Add Slot"))
                mr->materialSlots.emplace_back();
            if (!mr->materialSlots.empty()) {
                ImGui::SameLine();
                if (ImGui::SmallButton("- Remove Last"))
                    mr->materialSlots.pop_back();
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
        if (changed) scene.MarkMaterialDirty();
        return true;
    }

private:
    const Resource::AssetRegistry* m_registry = nullptr;
};

// ── MaterialOverrideDrawer ─────────────────────────────────────────────────────
class MaterialOverrideDrawer : public IComponentDrawer {
public:
    explicit MaterialOverrideDrawer(const Resource::AssetRegistry* reg  = nullptr,
                                    const MaterialManager*          matMgr = nullptr)
        : m_registry(reg), m_matMgr(matMgr) {}

    bool TryDraw(entt::registry& reg, entt::entity entity, Scene& scene) override {
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

        bool changed = false;
        ImGui::PushID("MatOvr");

        // Whole-material asset override
        ImGui::PushID("matAsset");
        if (DrawAssetIDField("Material Asset", mat->materialAsset, "Material", m_registry))
            changed = true;
        ImGui::PopID();

        // ── Scalar overrides ──────────────────────────────────────────────────
        if (!mat->scalars.empty()) {
            ImGui::SeparatorText("Scalar Overrides");
            std::string toRemove;
            for (auto& [paramName, val] : mat->scalars) {
                ImGui::PushID(paramName.c_str());
                const ParamDef* def = FindParamDef(paramName);
                const char*     labelStr = (def && !def->displayName.empty())
                                           ? def->displayName.c_str() : paramName.c_str();

                using T = RHI::ParamUIType;
                const T uit = def ? def->uiType : T::Inferred;

                // Label first, then widget fills remaining width minus the remove button.
                ImGui::TextUnformatted(labelStr);
                ImGui::SameLine();
                const float widgetW = std::max(30.f, ImGui::GetContentRegionAvail().x - 28.f);
                ImGui::SetNextItemWidth(widgetW);
                if (auto* f = std::get_if<float>(&val)) {
                    const float lo  = def ? def->minValue : 0.f;
                    const float hi  = def ? def->maxValue : 1.f;
                    const float spd = (hi - lo) * 0.005f;
                    changed |= ImGui::DragFloat("##v", f, spd, lo, hi);
                } else if (auto* v2 = std::get_if<glm::vec2>(&val)) {
                    changed |= ImGui::DragFloat2("##v", glm::value_ptr(*v2), 0.01f);
                } else if (auto* v3 = std::get_if<glm::vec3>(&val)) {
                    if (uit == T::Color3 || uit == T::Inferred)
                        changed |= ImGui::ColorEdit3("##v", glm::value_ptr(*v3));
                    else
                        changed |= ImGui::DragFloat3("##v", glm::value_ptr(*v3), 0.01f);
                } else if (auto* v4 = std::get_if<glm::vec4>(&val)) {
                    if (uit == T::Color4)
                        changed |= ImGui::ColorEdit4("##v", glm::value_ptr(*v4),
                                                     ImGuiColorEditFlags_Float);
                    else
                        changed |= ImGui::DragFloat4("##v", glm::value_ptr(*v4), 0.01f);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("-##rmS")) toRemove = paramName;
                ImGui::PopID();
            }
            if (!toRemove.empty()) { mat->scalars.erase(toRemove); changed = true; }
        }

        // ── Texture overrides ─────────────────────────────────────────────────
        if (!mat->textures.empty()) {
            ImGui::SeparatorText("Texture Overrides");
            std::string toRemoveTex;
            for (auto& [texName, texID] : mat->textures) {
                ImGui::PushID(texName.c_str());
                const TextureDef* tdef     = FindTextureDef(texName);
                const char*       labelStr = (tdef && !tdef->displayName.empty())
                                             ? tdef->displayName.c_str() : texName.c_str();
                if (ImGui::SmallButton("-##rmT")) { toRemoveTex = texName; }
                else {
                    ImGui::SameLine();
                    if (DrawAssetIDField(labelStr, texID, "Texture", m_registry))
                        changed = true;
                }
                ImGui::PopID();
            }
            if (!toRemoveTex.empty()) { mat->textures.erase(toRemoveTex); changed = true; }
        }

        // ── Add Override popup ────────────────────────────────────────────────
        if (ImGui::SmallButton("+ Add Override"))
            ImGui::OpenPopup("##add_ovr");
        if (ImGui::BeginPopup("##add_ovr")) {
            if (m_matMgr) {
                // Resolve the effective type from the assigned .mat asset so the
                // popup only shows params that belong to this material's shader.
                // Falls back to all types (deduped) if no asset is assigned.
                const MaterialType* eff = ResolveEffectiveType(mat);

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
                    for (const auto& [typeName, typePtr] : m_matMgr->GetTypes())
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
                    for (const auto& [typeName, typePtr] : m_matMgr->GetTypes()) {
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

private:
    const Resource::AssetRegistry* m_registry = nullptr;
    const MaterialManager*          m_matMgr   = nullptr;

    // Read "type" field from a .mat JSON file without a full JSON parser.
    [[nodiscard]] static std::string ReadMatTypeName(const std::filesystem::path& path) {
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

    // Resolve the MaterialType for the currently-assigned material asset.
    // Returns nullptr if no asset is assigned, the asset can't be found, or the
    // type hasn't been registered yet.
    [[nodiscard]] const MaterialType*
    ResolveEffectiveType(const MaterialOverrideComponent* mat) const {
        if (!mat || !mat->materialAsset.IsValid() || !m_registry || !m_matMgr)
            return nullptr;
        const auto* entry = m_registry->FindByID(mat->materialAsset);
        if (!entry) return nullptr;
        const std::string typeName = ReadMatTypeName(entry->sourcePath);
        if (typeName.empty()) return nullptr;
        return m_matMgr->GetType(typeName);
    }

    // Search all registered types for a ParamDef by name.
    [[nodiscard]] const ParamDef* FindParamDef(const std::string& name) const {
        if (!m_matMgr) return nullptr;
        for (const auto& [tname, tptr] : m_matMgr->GetTypes())
            for (const auto& p : tptr->params)
                if (p.name == name) return &p;
        return nullptr;
    }

    // Search all registered types for a TextureDef by name.
    [[nodiscard]] const TextureDef* FindTextureDef(const std::string& name) const {
        if (!m_matMgr) return nullptr;
        for (const auto& [tname, tptr] : m_matMgr->GetTypes())
            for (const auto& t : tptr->textures)
                if (t.name == name) return &t;
        return nullptr;
    }
};

// ── RigidBodyDrawer ────────────────────────────────────────────────────────────
class RigidBodyDrawer : public IComponentDrawer {
public:
    bool TryDraw(entt::registry& reg, entt::entity entity, Scene& /*scene*/) override {
        auto* rb = reg.try_get<RigidBodyComponent>(entity);
        if (!rb) return false;
        bool open = ImGui::CollapsingHeader("Rigid Body", HeaderFlags());
        if (RemoveButton("x##rem_rb")) { reg.remove<RigidBodyComponent>(entity); return true; }
        if (!open) return true;

        const char* types[] = { "Static", "Kinematic", "Dynamic" };
        int t = static_cast<int>(rb->type);
        if (ImGui::Combo("Type", &t, types, 3))
            rb->type = static_cast<RigidBodyComponent::Type>(t);

        ImGui::BeginDisabled(rb->type != RigidBodyComponent::Type::Dynamic);
        ImGui::DragFloat("Mass",        &rb->mass,        0.1f, 0.001f, 10000.f);
        ImGui::EndDisabled();
        ImGui::DragFloat("Friction",    &rb->friction,    0.01f, 0.f, 1.f);
        ImGui::DragFloat("Restitution", &rb->restitution, 0.01f, 0.f, 1.f);

        if (rb->bodyId != ~0u)
            ImGui::LabelText("Body ID", "%u", rb->bodyId & 0xFFFFu);
        else
            ImGui::LabelText("Body ID", "(not created)");
        return true;
    }
};

// ── ColliderDrawer ─────────────────────────────────────────────────────────────
class ColliderDrawer : public IComponentDrawer {
public:
    bool TryDraw(entt::registry& reg, entt::entity entity, Scene& /*scene*/) override {
        auto* col = reg.try_get<ColliderComponent>(entity);
        if (!col) return false;
        bool open = ImGui::CollapsingHeader("Collider", HeaderFlags());
        if (RemoveButton("x##rem_col")) { reg.remove<ColliderComponent>(entity); return true; }
        if (!open) return true;

        const char* shapes[] = { "Box", "Sphere", "Capsule" };
        int s = static_cast<int>(col->shape);
        if (ImGui::Combo("Shape", &s, shapes, 3))
            col->shape = static_cast<ColliderComponent::Shape>(s);

        switch (col->shape) {
            case ColliderComponent::Shape::Box:
                ImGui::DragFloat3("Half Extents", glm::value_ptr(col->extents),
                                  0.01f, 0.001f, 100.f);
                break;
            case ColliderComponent::Shape::Sphere:
                ImGui::DragFloat("Radius", &col->extents.x, 0.01f, 0.001f, 100.f);
                break;
            case ColliderComponent::Shape::Capsule:
                ImGui::DragFloat("Radius",      &col->extents.x, 0.01f, 0.001f, 100.f);
                ImGui::DragFloat("Half Height", &col->extents.y, 0.01f, 0.001f, 100.f);
                break;
        }

        ImGui::Separator();
        ImGui::DragFloat3("Center Offset", glm::value_ptr(col->offset), 0.01f);

        // Euler edit for shape orientation (applied on body creation / restart).
        glm::vec3 euler = glm::degrees(glm::eulerAngles(col->rotation));
        if (ImGui::DragFloat3("Orientation (deg)", glm::value_ptr(euler), 0.5f))
            col->rotation = glm::quat(glm::radians(euler));

        return true;
    }
};

} // namespace StellarAlia::Editor
