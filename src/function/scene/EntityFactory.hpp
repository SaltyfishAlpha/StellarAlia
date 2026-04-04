#pragma once

#include <string_view>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/asset/AssetID.hpp"

namespace StellarAlia {

class Scene;

// ─────────────────────────────────────────────────────────────────────────────
// EntityFactory
//
// Stateless factory for common entity archetypes. Every method creates a fully
// initialised entity in the given Scene: TagComponent + TransformComponent +
// WorldTransformComponent + the archetype-specific components — then calls
// Scene::MarkDirty so the transform is recomputed on the next UpdateTransforms.
//
// Usage:
//   auto sun = EntityFactory::CreateDirectionalLight(scene, "Sun",
//       {1.f, 0.95f, 0.85f}, 2.f,
//       glm::normalize(glm::angleAxis(glm::radians(-45.f), glm::vec3{1,0,0})));
//
// Extending:
//   Add a static method here and a corresponding block in EntityFactory.cpp.
//   Do not store any state — all methods are pure Scene mutations.
// ─────────────────────────────────────────────────────────────────────────────
struct EntityFactory {
    // ── Lights ────────────────────────────────────────────────────────────────

    // Directional light (infinite distance, no position, direction = rotation × -Z).
    static entt::entity CreateDirectionalLight(
        Scene&          scene,
        std::string_view name,
        glm::vec3        color     = {1.f, 1.f, 1.f},
        float            intensity = 1.f,
        glm::quat        rotation  = glm::quat{1.f, 0.f, 0.f, 0.f},
        bool             castShadow = false);

    // Point light (positional, isotropic, range in world units).
    static entt::entity CreatePointLight(
        Scene&          scene,
        std::string_view name,
        glm::vec3        color     = {1.f, 1.f, 1.f},
        float            intensity = 1.f,
        float            range     = 10.f,
        glm::vec3        position  = {0.f, 0.f, 0.f});

    // Spot light (positional, cone, direction = rotation × -Z).
    static entt::entity CreateSpotLight(
        Scene&          scene,
        std::string_view name,
        glm::vec3        color      = {1.f, 1.f, 1.f},
        float            intensity  = 1.f,
        float            range      = 10.f,
        float            innerAngle = glm::radians(15.f),
        float            outerAngle = glm::radians(30.f),
        glm::vec3        position   = {0.f, 0.f, 0.f},
        glm::quat        rotation   = glm::quat{1.f, 0.f, 0.f, 0.f});

    // Rectangle area light (LTC). The rectangle lies in the entity's local XZ plane:
    //   local +X → tangentU (width),  local +Z → tangentV (height),  local +Y → emission normal.
    //
    // withMesh = true  (default): also attaches StaticMeshComponent (built-in plane) +
    //   pitch-black PBRSurfaceComponent + MaterialParamComponent (emissiveFactor = color × emissiveScale).
    //   Scale is set to (size.x, kAreaLightPanelThickness, size.y) so the mesh covers the rectangle.
    //
    // withMesh = false: only AreaLightComponent is attached (pure invisible light source).
    //   Scale is set to (size.x, 1, size.y) to keep the light rectangle correct.
    //   emissiveScale is ignored.
    static entt::entity CreateAreaLight(
        Scene&          scene,
        std::string_view name,
        glm::vec3        color         = {1.f, 1.f, 1.f},
        float            intensity     = 4.f,
        glm::vec2        size          = {1.f, 1.f},   // width × height, metres
        glm::vec3        position      = {0.f, 0.f, 0.f},
        glm::quat        rotation      = glm::quat{1.f, 0.f, 0.f, 0.f},
        bool             twoSided      = false,
        float            emissiveScale = 2.f,           // mesh brightness multiplier (withMesh only)
        bool             withMesh      = true);

    // ── Geometry ──────────────────────────────────────────────────────────────

    // Static mesh entity. materialSlots may be empty (SceneRenderer falls back
    // to the submesh defaultMaterialID, then neutral PBR).
    static entt::entity CreateStaticMesh(
        Scene&                   scene,
        std::string_view         name,
        AssetID                  meshAsset,
        glm::vec3                position      = {0.f, 0.f, 0.f},
        glm::quat                rotation      = glm::quat{1.f, 0.f, 0.f, 0.f},
        glm::vec3                scale         = {1.f, 1.f, 1.f},
        std::vector<AssetID>     materialSlots = {});

    // ── Camera ────────────────────────────────────────────────────────────────

    // Perspective camera. If makeActive is true, also attaches ActiveCameraTag.
    // Only one entity should be active at a time; callers must remove the tag
    // from the previous active camera themselves (or use Scene::SetActiveCamera
    // when that helper is added in Stage 7).
    static entt::entity CreateCamera(
        Scene&          scene,
        std::string_view name,
        float            fovY      = glm::radians(60.f),
        float            nearPlane = 0.1f,
        float            farPlane  = 1000.f,
        glm::vec3        position  = {0.f, 0.f, 0.f},
        glm::quat        rotation  = glm::quat{1.f, 0.f, 0.f, 0.f},
        bool             makeActive = true);
};

} // namespace StellarAlia
