#pragma once

#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/asset/AssetID.hpp"
#include "platform/rhi/RHITypes.hpp"

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
    AssetID meshAsset;  // → .samesh
};

// Shared rendering configuration for any mesh entity (static or skinned).
// Material slot overrides + per-object shadow flags live here so both mesh
// types share the same settings without duplication.
struct MeshRendererComponent {
    std::vector<AssetID> materialSlots;    // per-submesh override → .samat; empty = use mesh defaults
    bool                 castShadow    = true;
    bool                 receiveShadow = true;
};

// ── Camera ────────────────────────────────────────────────────────────────────

struct CameraComponent {
    float fovY      = glm::radians(60.f);  // vertical FOV in radians
    float nearPlane = 0.1f;
    float farPlane  = 1000.f;
    int   priority  = 0;  // highest priority camera is the primary view; ties: first found
    // Aspect ratio is derived from the swapchain at render time.
};

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
// Unified override component. When present, the render system clones the base
// MaterialInstance and applies overrides. Entities without this component share
// the cached instance directly (no clone, no allocation).
//
//   materialAsset — replaces the resolved base material for all sub-meshes;
//                   invalid = keep using the mesh-default or MeshRenderer slot.
//   scalars       — named UBO parameter overrides (names match shader ParamDefs).
//   textures      — named texture slot overrides (names match shader TextureDefs).
//
using ParamValue = std::variant<float, glm::vec2, glm::vec3, glm::vec4>;

struct MaterialOverrideComponent {
    AssetID                           materialAsset;
    std::map<std::string, ParamValue> scalars;
    std::map<std::string, AssetID>    textures;
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

// Drives keyframe playback for a skinned entity.
struct AnimatorComponent {
    AssetID clipAsset;      // → .saanim; set via .sanim asset picker or DeriveAnimID fallback
    float   time     = 0.f; // current playback position in seconds
    float   speed    = 1.f;
    bool    looping  = true;
    bool    playing  = true;
};

// Per-sub-mesh draw call descriptor for a skinned mesh entity.
struct SkinnedSubMeshInfo {
    uint32_t firstIndex   = 0;
    uint32_t indexCount   = 0;
    int32_t  vertexOffset = 0;
    AssetID  materialAssetID;   // .samat uuid; invalid → use default material
};

// Holds the GPU-side skinned mesh data.
//   dynVertexBuffer: CPU-visible, written each frame by AnimationSystem (deformed poses).
//   indexBuffer:     Static GPU buffer; shared from LoadMesh().
//   ready:           Set true by AnimationSystem::PrepareEntity(); BuildDrawList skips if false.
struct SkinnedMeshComponent {
    AssetID                         meshAsset;
    RHI::RHIBufferHandle            dynVertexBuffer;
    RHI::RHIBufferHandle            indexBuffer;
    uint32_t                        vertexCount = 0;
    std::vector<SkinnedSubMeshInfo> subMeshes;
    bool                            ready = false;
};

// ── Physics ───────────────────────────────────────────────────────────────────
//
// RigidBodyComponent — driven by PhysicsSystem.
//   bodyId is written by PhysicsSystem::SyncIn on first encounter (~0u = not created yet).
//
struct RigidBodyComponent {
    enum class Type { Static, Kinematic, Dynamic };
    Type     type        = Type::Dynamic;
    float    mass        = 1.f;
    float    friction    = 0.5f;
    float    restitution = 0.f;
    uint32_t bodyId      = ~0u;   // Jolt BodyID bits; ~0u = not yet created
};

// ColliderComponent — defines the collision shape.
//   extents interpretation:
//     Box:     half-extents (x, y, z)
//     Sphere:  radius in x  (y, z ignored)
//     Capsule: radius in x, half-height in y
//   offset   — center of the shape in entity local space (applied on body creation).
//   rotation — orientation of the shape in entity local space.
struct ColliderComponent {
    enum class Shape { Box, Sphere, Capsule };
    Shape     shape    = Shape::Box;
    glm::vec3 extents  = { 0.5f, 0.5f, 0.5f };
    glm::vec3 offset   = { 0.f, 0.f, 0.f };
    glm::quat rotation = glm::quat{ 1.f, 0.f, 0.f, 0.f };
};

// ── Marker tags ───────────────────────────────────────────────────────────────

// Hint to culling/lightmap/BVH systems: this entity never moves.
struct StaticGeometryTag {};

} // namespace StellarAlia
