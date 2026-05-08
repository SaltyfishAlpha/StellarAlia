#pragma once

#include "ui/IComponentDrawer.hpp"
#include "function/scene/Scene.hpp"
#include "function/scene/Components.hpp"
#include "resource/AssetRegistry.hpp"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdio>
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

    // Button opens the picker popup.
    const float btnWidth = ImGui::GetContentRegionAvail().x - (id.IsValid() ? 26.f : 0.f);
    ImGui::SetNextItemWidth(btnWidth);
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

    ImGui::SameLine(0, 4);
    ImGui::TextUnformatted(label);

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
        changed |= ImGui::DragFloat3("Scale", glm::value_ptr(tr->scale), 0.01f, 0.001f, 1000.f);
        if (changed) scene.MarkDirty(entity);
        return true;
    }

private:
    uint32_t  m_cachedEulerEntity = ~0u;
    glm::vec3 m_cachedEuler       = {};
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
        ImGui::DragFloat("Near", &cam->nearPlane, 0.001f, 0.001f,  10.f);
        ImGui::DragFloat("Far",  &cam->farPlane,  1.f,    1.f,    10000.f);
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
            sm->ready       = false;
            sm->vertexCount = 0;
            sm->subMeshes.clear();
            scene.MarkSkinnedMeshDirty();
        }
        ImGui::LabelText("Vertices",   "%u",  sm->vertexCount);
        ImGui::LabelText("Sub Meshes", "%zu", sm->subMeshes.size());
        ImGui::LabelText("Status",     "%s",  sm->ready ? "Ready" : "Pending");
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

// ── PBRSurfaceDrawer ───────────────────────────────────────────────────────────
class PBRSurfaceDrawer : public IComponentDrawer {
public:
    explicit PBRSurfaceDrawer(const Resource::AssetRegistry* reg = nullptr)
        : m_registry(reg) {}

    bool TryDraw(entt::registry& reg, entt::entity entity, Scene& scene) override {
        auto* pbr = reg.try_get<PBRSurfaceComponent>(entity);
        if (!pbr) return false;
        bool open = ImGui::CollapsingHeader("PBR Surface", HeaderFlags(ImGuiTreeNodeFlags_DefaultOpen));
        if (RemoveButton("x##rem_pbr")) {
            reg.remove<PBRSurfaceComponent>(entity);
            scene.MarkMaterialDirty();
            return true;
        }
        if (!open) return true;
        bool changed = false;
        changed |= ImGui::ColorEdit4("Base Color", glm::value_ptr(pbr->baseColor), ImGuiColorEditFlags_Float);
        changed |= ImGui::DragFloat("Roughness",   &pbr->roughness, 0.01f, 0.f, 1.f);
        changed |= ImGui::DragFloat("Metallic",    &pbr->metallic,  0.01f, 0.f, 1.f);
        changed |= DrawAssetIDField("Albedo Map", pbr->albedoMap, "Texture", m_registry);
        changed |= DrawAssetIDField("Normal Map", pbr->normalMap, "Texture", m_registry);
        if (changed) scene.MarkMaterialDirty();
        return true;
    }

private:
    const Resource::AssetRegistry* m_registry = nullptr;
};

// ── MaterialParamDrawer ────────────────────────────────────────────────────────
class MaterialParamDrawer : public IComponentDrawer {
public:
    explicit MaterialParamDrawer(const Resource::AssetRegistry* reg = nullptr)
        : m_registry(reg) {}

    bool TryDraw(entt::registry& reg, entt::entity entity, Scene& scene) override {
        auto* mp = reg.try_get<MaterialParamComponent>(entity);
        if (!mp) return false;
        bool open = ImGui::CollapsingHeader("Material Params", HeaderFlags());
        if (RemoveButton("x##rem_mp")) {
            reg.remove<MaterialParamComponent>(entity);
            scene.MarkMaterialDirty();
            return true;
        }
        if (!open) return true;
        bool changed = false;
        for (auto& [name, val] : mp->scalars) {
            ImGui::PushID(name.c_str());
            if (auto* f = std::get_if<float>(&val))
                changed |= ImGui::DragFloat(name.c_str(), f, 0.01f);
            else if (auto* v2 = std::get_if<glm::vec2>(&val))
                changed |= ImGui::DragFloat2(name.c_str(), glm::value_ptr(*v2), 0.01f);
            else if (auto* v3 = std::get_if<glm::vec3>(&val))
                changed |= ImGui::DragFloat3(name.c_str(), glm::value_ptr(*v3), 0.01f);
            else if (auto* v4 = std::get_if<glm::vec4>(&val))
                changed |= ImGui::DragFloat4(name.c_str(), glm::value_ptr(*v4), 0.01f);
            ImGui::PopID();
        }
        for (auto& [name, tex] : mp->textures)
            changed |= DrawAssetIDField(name.c_str(), tex, "Texture", m_registry);
        if (changed) scene.MarkMaterialDirty();
        return true;
    }

private:
    const Resource::AssetRegistry* m_registry = nullptr;
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
