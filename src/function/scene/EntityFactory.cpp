#include "function/scene/EntityFactory.hpp"

#include "function/scene/Components.hpp"
#include "function/scene/Scene.hpp"

namespace StellarAlia {

// ── Internal helper ───────────────────────────────────────────────────────────
// Creates the base entity with Tag + Transform + WorldTransform and returns it.

static entt::entity MakeBase(Scene& scene, std::string_view name,
                              glm::vec3 position, glm::quat rotation,
                              glm::vec3 scale)
{
    auto e = scene.CreateEntity(std::string(name));
    auto& t = scene.Registry().get<TransformComponent>(e);
    t.position = position;
    t.rotation = rotation;
    t.scale    = scale;
    scene.MarkDirty(e);
    return e;
}

// ── Lights ────────────────────────────────────────────────────────────────────

entt::entity EntityFactory::CreateDirectionalLight(
    Scene& scene, std::string_view name,
    glm::vec3 color, float intensity,
    glm::quat rotation, bool castShadow)
{
    auto e = MakeBase(scene, name, {0.f, 0.f, 0.f}, rotation, {1.f, 1.f, 1.f});
    scene.Registry().emplace<DirectionalLightComponent>(e,
        DirectionalLightComponent{color, intensity, castShadow});
    return e;
}

entt::entity EntityFactory::CreatePointLight(
    Scene& scene, std::string_view name,
    glm::vec3 color, float intensity, float range,
    glm::vec3 position)
{
    auto e = MakeBase(scene, name, position, {1.f, 0.f, 0.f, 0.f}, {1.f, 1.f, 1.f});
    scene.Registry().emplace<PointLightComponent>(e,
        PointLightComponent{color, intensity, range});
    return e;
}

entt::entity EntityFactory::CreateSpotLight(
    Scene& scene, std::string_view name,
    glm::vec3 color, float intensity, float range,
    float innerAngle, float outerAngle,
    glm::vec3 position, glm::quat rotation)
{
    auto e = MakeBase(scene, name, position, rotation, {1.f, 1.f, 1.f});
    scene.Registry().emplace<SpotLightComponent>(e,
        SpotLightComponent{color, intensity, range, innerAngle, outerAngle});
    return e;
}

// ── Geometry ──────────────────────────────────────────────────────────────────

entt::entity EntityFactory::CreateStaticMesh(
    Scene& scene, std::string_view name,
    AssetID meshAsset,
    glm::vec3 position, glm::quat rotation, glm::vec3 scale,
    std::vector<AssetID> materialSlots)
{
    auto e   = MakeBase(scene, name, position, rotation, scale);
    auto& reg = scene.Registry();
    reg.emplace<StaticMeshComponent>(e, StaticMeshComponent{meshAsset});
    MeshRendererComponent mr;
    mr.materialSlots = std::move(materialSlots);
    reg.emplace<MeshRendererComponent>(e, std::move(mr));
    return e;
}

// ── Area light ────────────────────────────────────────────────────────────────

// Built-in plane mesh: 1×1 unit quad in the local XZ plane, Y-up normal.
static constexpr std::string_view kBuiltinPlaneMeshUUID = "c0be0000-0000-4000-0000-000000000002";
// Visible panel depth (world units) — thin enough to be imperceptible.
static constexpr float kAreaLightPanelThickness = 0.05f;

entt::entity EntityFactory::CreateAreaLight(
    Scene& scene, std::string_view name,
    glm::vec3 color, float intensity, glm::vec2 size,
    glm::vec3 position, glm::quat rotation,
    bool twoSided, float emissiveScale, bool withMesh)
{
    const float     yScale = withMesh ? kAreaLightPanelThickness : 1.f;
    const glm::vec3 scale{size.x, yScale, size.y};
    auto  e   = MakeBase(scene, name, position, rotation, scale);
    auto& reg = scene.Registry();

    reg.emplace<AreaLightComponent>(e, AreaLightComponent{
        color, intensity, size, twoSided, emissiveScale});

    if (withMesh) {
        reg.emplace<StaticMeshComponent>(e, StaticMeshComponent{
            AssetID::FromString(kBuiltinPlaneMeshUUID)});
        reg.emplace<MeshRendererComponent>(e, MeshRendererComponent{
            {}, /*castShadow=*/false, /*receiveShadow=*/false});

        reg.emplace<PBRSurfaceComponent>(e, PBRSurfaceComponent{
            /*baseColor=*/{0.f, 0.f, 0.f, 1.f},
            /*roughness=*/1.f,
            /*metallic=*/0.f});

        MaterialParamComponent mp;
        mp.scalars["emissiveFactor"] = color * emissiveScale;
        reg.emplace<MaterialParamComponent>(e, std::move(mp));
    }

    return e;
}

// ── Camera ────────────────────────────────────────────────────────────────────

entt::entity EntityFactory::CreateCamera(
    Scene& scene, std::string_view name,
    float fovY, float nearPlane, float farPlane,
    glm::vec3 position, glm::quat rotation,
    bool makeActive)
{
    auto e = MakeBase(scene, name, position, rotation, {1.f, 1.f, 1.f});
    scene.Registry().emplace<CameraComponent>(e,
        CameraComponent{fovY, nearPlane, farPlane});
    if (makeActive)
        scene.Registry().emplace<ActiveCameraTag>(e);
    return e;
}

} // namespace StellarAlia
