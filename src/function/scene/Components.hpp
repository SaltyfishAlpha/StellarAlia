#pragma once

#include <cstring>
#include <string>
#include <unordered_map>
#include <variant>
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

// Rectangle area light. Position/orientation from TransformComponent.
//
// Axis convention (matches the builtin plane mesh geometry):
//   local +X  → tangentU  (width  direction of the rectangle)
//   local +Z  → tangentV  (height direction of the rectangle)
//   local +Y  → emission normal (light faces in +Y direction)
//
// The entity's scale is set to (size.x, panel_thickness, size.y) by SpawnAreaLight
// so that the accompanying emissive StaticMesh exactly covers the light rectangle.
//
// Evaluated via LTC (Linearly Transformed Cosines) in the PBR shader.
struct AreaLightComponent {
    glm::vec3 color         = { 1.f, 1.f, 1.f };
    float     intensity     = 1.f;
    glm::vec2 size          = { 1.f, 1.f };  // width (X) × height (Z) in world space
    bool      twoSided      = false;
    float     emissiveScale = 2.f;            // visible mesh brightness = color × emissiveScale
};

// ── Material overrides ────────────────────────────────────────────────────────
//
// Two-tier override system. Both components are optional and independent.
// The render system clones the base MaterialInstance on first override, then
// applies whichever components are present. Entities with no override components
// share the cached instance directly (no clone, no allocation).
//
// Tier 1 — PBRSurfaceComponent
//   Typed fast path for the built-in PBR shader. No string lookups at runtime.
//   Only set fields that differ from the .samat default; an invalid AssetID means
//   "keep the texture from the base material".
//
struct PBRSurfaceComponent {
    glm::vec4 baseColor = {1.f, 1.f, 1.f, 1.f};
    float     roughness = 0.5f;
    float     metallic  = 0.f;
    AssetID   albedoMap;   // invalid → keep base material texture
    AssetID   normalMap;   // invalid → keep base material texture
};

//
// Tier 2 — MaterialParamComponent
//   Generic path for any shader (custom, modified PBR, toon, …).
//   Parameter names must match the MaterialType's ParamDef / TextureDef names.
//   Can be combined with PBRSurfaceComponent: PBR fields apply first, then these.
//
using ParamValue = std::variant<float, glm::vec2, glm::vec3, glm::vec4>;

struct MaterialParamComponent {
    std::unordered_map<std::string, ParamValue> scalars;   // name → UBO value
    std::unordered_map<std::string, AssetID>    textures;  // name → sampler AssetID
};

// ── Animation ─────────────────────────────────────────────────────────────────
//
// AnimatedTransformComponent — per-frame animated local pose.
//   Written each frame by the animation system; never serialized.
//   UpdateTransforms uses this in preference to TransformComponent when present.
//   Entities without animation carry no overhead (component not attached).
//
struct AnimatedTransformComponent {
    glm::vec3 position = { 0.f, 0.f, 0.f };
    glm::quat rotation = { 1.f, 0.f, 0.f, 0.f };
    glm::vec3 scale    = { 1.f, 1.f, 1.f };
};

// ── Marker tags ───────────────────────────────────────────────────────────────

// Hint to culling/lightmap/BVH systems: this entity never moves.
struct StaticGeometryTag {};

} // namespace StellarAlia
