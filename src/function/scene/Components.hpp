#pragma once

#include <cstring>
#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/asset/AssetID.hpp"

namespace StellarAlia {

// ── Identification ────────────────────────────────────────────────────────────

struct TagComponent {
    std::string name;
};

// ── Hierarchy ─────────────────────────────────────────────────────────────────
// Only added to entities that actually participate in a parent/child relationship.
// Leaf entities (no parent, no children) have no HierarchyComponent.

struct HierarchyComponent {
    entt::entity              parent   = entt::null;
    std::vector<entt::entity> children;
};

// ── Transform ─────────────────────────────────────────────────────────────────

struct TransformComponent {
    glm::vec3 position = { 0.f, 0.f, 0.f };
    glm::quat rotation = { 1.f, 0.f, 0.f, 0.f };  // identity (w, x, y, z)
    glm::vec3 scale    = { 1.f, 1.f, 1.f };
};

// Derived world-space matrix — recomputed by Scene::UpdateTransforms().
// Not serialized to disk; always reconstructed at load time.
struct WorldTransformComponent {
    glm::mat4 matrix = glm::mat4(1.f);
    bool      dirty  = true;
};

// ── Rendering ─────────────────────────────────────────────────────────────────

struct StaticMeshComponent {
    AssetID              meshAsset;        // → .samesh
    std::vector<AssetID> materialSlots;    // one per submesh → .samat
    bool                 castShadow    = true;
    bool                 receiveShadow = true;
};

// ── Camera ────────────────────────────────────────────────────────────────────

struct CameraComponent {
    float fovY      = glm::radians(60.f);  // vertical FOV in radians
    float nearPlane = 0.1f;
    float farPlane  = 1000.f;
    // Aspect ratio is derived from the swapchain at render time.
};

// Marks the entity whose CameraComponent is used for the primary view.
// At most one entity should carry this tag at a time.
struct ActiveCameraTag {};

// ── Lights ────────────────────────────────────────────────────────────────────
// Direction/position is taken from the entity's TransformComponent.
// DirectionalLight: forward = -Z of entity rotation.
// PointLight / SpotLight: position = entity translation.

struct DirectionalLightComponent {
    glm::vec3 color      = { 1.f, 1.f, 1.f };
    float     intensity  = 1.f;
    bool      castShadow = false;
};

struct PointLightComponent {
    glm::vec3 color     = { 1.f, 1.f, 1.f };
    float     intensity = 1.f;
    float     range     = 10.f;
};

struct SpotLightComponent {
    glm::vec3 color      = { 1.f, 1.f, 1.f };
    float     intensity  = 1.f;
    float     range      = 10.f;
    float     innerAngle = glm::radians(15.f);  // radians, from axis
    float     outerAngle = glm::radians(30.f);
};

// ── Environment ───────────────────────────────────────────────────────────────

struct SkyboxComponent {
    AssetID cubemapAsset;   // → .satex (cube map)
};

struct IBLComponent {
    AssetID irradianceMap;      // low-frequency diffuse (small cubemap)
    AssetID prefilteredEnvMap;  // specular mip chain
    AssetID brdfLut;            // 2D split-sum LUT
};

// ── Material overrides ────────────────────────────────────────────────────────
//
// Optional per-entity overrides applied on top of the base .samat asset.
// The render system resolves StaticMeshComponent::materialSlots → MaterialInstance,
// then calls MaterialInstance::SetRawParam() for each entry here.
//
// Values are stored as raw bytes; the size and interpretation come from the
// ShaderMemberDesc in the merged reflection (same data that built ParamDef).
// Use SetParam<T> helpers to fill entries in a type-safe way:
//
//   auto& ov = reg.emplace<MaterialOverrideComponent>(e);
//   MaterialOverrideComponent::Set(ov.params, "roughnessFactor", 0.1f);
//
struct MaterialOverrideComponent {
    struct Param {
        std::string          name;
        std::vector<uint8_t> value;  // raw bytes — sizeof(T) bytes for a given T
    };
    std::vector<Param> params;

    // Convenience: upsert a typed value by name.
    template<typename T>
    static void Set(std::vector<Param>& params, std::string_view name, const T& v) {
        for (auto& p : params) {
            if (p.name == name) {
                p.value.resize(sizeof(T));
                std::memcpy(p.value.data(), &v, sizeof(T));
                return;
            }
        }
        Param p;
        p.name = std::string(name);
        p.value.resize(sizeof(T));
        std::memcpy(p.value.data(), &v, sizeof(T));
        params.push_back(std::move(p));
    }
};

// ── Marker tags ───────────────────────────────────────────────────────────────

// Hint to culling/lightmap/BVH systems: this entity never moves.
struct StaticGeometryTag {};

} // namespace StellarAlia
