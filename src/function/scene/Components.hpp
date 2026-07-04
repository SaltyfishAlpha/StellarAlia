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
#include "function/script/ScriptFieldSchema.hpp"
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

// Issue #101: overrides scoped to a single sub-mesh (keyed by submesh index in
// MaterialOverrideComponent::slotOverrides). Applied on top of the entity-wide
// fields, so a slot entry only needs the params that differ.
struct MaterialSlotOverride {
    std::map<std::string, ParamValue> scalars;
    std::map<std::string, AssetID>    textures;
    int8_t                            alphaMode   = -1;   // -1 = inherit
    int8_t                            doubleSided = -1;
};

struct MaterialOverrideComponent {
    AssetID                           materialAsset;
    std::map<std::string, ParamValue> scalars;
    std::map<std::string, AssetID>    textures;
    // Issue #56: per-entity pipeline-state overrides. -1 = inherit from the
    // material asset; alphaMode 0/1/2 = Opaque/Mask/Blend (AlphaMode enum),
    // doubleSided 0/1 = off/on. int8 sentinels keep serialization trivial.
    int8_t                            alphaMode   = -1;
    int8_t                            doubleSided = -1;
    // Issue #101: per-submesh overrides layered after the entity-wide ones.
    std::map<int32_t, MaterialSlotOverride> slotOverrides;
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

// Holds the GPU-side per-entity skinning state.
//   skinMatricesBuffer:  Per-bone 4×4 matrices, CPU-visible, uploaded each frame by AnimationSystem.
//   skinDescSet:         set=2 descriptor (binding0=skinMats, binding1=GPUMesh::skinDataBuffer).
//   boneCount:           Number of bones; determines skinMatricesBuffer size.
//   ready:               Set true by AnimationSystem::PrepareEntity(); BuildDrawList skips if false.
// Shared mesh data (vertexBuffer, indexBuffer, subMeshes, skinDataBuffer) lives in GPUMesh.
struct SkinnedMeshComponent {
    AssetID               meshAsset;
    RHI::RHIBufferHandle  skinMatricesBuffer;        // current-frame bone matrices
    RHI::RHIBufferHandle  skinMatricesBufferPrev;    // previous-frame bone matrices (Issue #84)
    RHI::RHIDescSetHandle skinDescSet;
    RHI::RHIDescSetHandle velocityDescSet;           // set=3 with bindings 0/1/2 (curr/skinData/prev) for VelocityPrepass
    uint32_t              boneCount   = 0;
    bool                  ready       = false;
    bool                  poseSeeded  = false;       // Issue #84: first-eval guard; cleared on PrepareEntity / clip swap
    AssetID               lastEvalClipId;            // Issue #84: detect clip swap → force prev=curr that frame
};

// ── PrevTransform (Issue #84) ────────────────────────────────────────────────
// Captures the previous frame's WorldTransformComponent.matrix. Stamped at the
// top of Scene::UpdateTransforms for seeded entities, seeded at tail for fresh
// entities (velocity=0 on first frame). VelocityPrepass reads this to compute
// per-object screen-space velocity = currUV − prevUV.
struct PrevTransformComponent {
    glm::mat4 prevModel = glm::mat4(1.f);
    bool      seeded    = false;
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

// ── Scripting ─────────────────────────────────────────────────────────────────

struct ScriptComponent {
    AssetID     scriptId;     // .cs asset UUID; resolves to path via AssetRegistry::FindByID
    std::string className;    // C# class name — derived from filename stem if empty
    // Per-field value cache; Inspector writes into this in Edit mode and
    // ScriptSystem injects it into the C# instance at Play start (and on each
    // edit during Play, see ScriptSystem::InjectFieldValues).
    std::unordered_map<std::string, ScriptFieldValue> fields;
};

// ── Stable scene-local identity (for EntityRef script fields) ─────────────────

// Scene-local 64-bit ID assigned by Scene::CreateEntity. Survives across
// save/load — used by script field `EntityRef` (#75) so cross-entity references
// in scripts persist when entt::entity values change between sessions.
// 0 is reserved for "unassigned" (legacy entities loaded before #75 ship).
struct EntityIdComponent {
    uint64_t sceneLocalId = 0;
};

// ── Marker tags ───────────────────────────────────────────────────────────────

// Hint to culling/lightmap/BVH systems: this entity never moves.
struct StaticGeometryTag {};

} // namespace StellarAlia
