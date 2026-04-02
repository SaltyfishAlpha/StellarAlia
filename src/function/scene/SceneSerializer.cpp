// SceneSerializer — .sascene JSON format
//
// Coordinate convention: right-hand Y-up (+X=right, +Y=up, +Z=toward viewer).
// Rotation quaternions are stored as [w, x, y, z].
//
// Schema (version 1):
// {
//   "version": 1,
//   "name": "MyLevel",
//   "entities": [
//     {
//       "id": 0,                           // serialization index (not entt value)
//       "tag": "MainCamera",
//       "parent": -1,                      // -1 = root; otherwise index into "entities"
//       "transform": {
//         "position": [0, 1.5, 5],
//         "rotation": [1, 0, 0, 0],        // [w, x, y, z]
//         "scale":    [1, 1, 1]
//       },
//       "camera": { "fovY": 1.047, "near": 0.1, "far": 1000.0 },
//       "activeCamera": true,
//       "staticMesh": {
//         "mesh": "550e8400-...",
//         "materials": ["6ba7b810-..."],
//         "castShadow": true,
//         "receiveShadow": true
//       },
//       "directionalLight": { "color": [1,1,1], "intensity": 3.0, "castShadow": false },
//       "pointLight":        { "color": [1,1,1], "intensity": 1.0, "range": 10.0 },
//       "spotLight":         { "color": [1,1,1], "intensity": 1.0, "range": 10.0,
//                              "innerAngle": 0.26, "outerAngle": 0.52 },
//       "skybox": { "cubemap": "uuid..." },
//       "ibl":    { "irradiance": "uuid...", "prefilteredEnv": "uuid...", "brdfLut": "uuid..." },
//       "staticGeometry": true
//     }
//   ]
// }

#include "function/scene/SceneSerializer.hpp"
#include "function/scene/Scene.hpp"
#include "core/logs/Log.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <unordered_map>

namespace StellarAlia {

using json = nlohmann::json;

// ── JSON helpers ──────────────────────────────────────────────────────────────

static json Vec3ToJson(const glm::vec3& v) { return { v.x, v.y, v.z }; }
static json QuatToJson(const glm::quat& q) { return { q.w, q.x, q.y, q.z }; }

static glm::vec3 JsonToVec3(const json& j) {
    return { j[0].get<float>(), j[1].get<float>(), j[2].get<float>() };
}
static glm::vec3 JsonToVec3Color(const json& j) { return JsonToVec3(j); }
static glm::quat JsonToQuat(const json& j) {
    return { j[0].get<float>(), j[1].get<float>(),
             j[2].get<float>(), j[3].get<float>() };  // w, x, y, z
}

static std::string AssetToStr(const AssetID& id) { return id.ToString(); }
static AssetID     StrToAsset(const std::string& s) { return AssetID::FromString(s); }

// ── Save ──────────────────────────────────────────────────────────────────────

bool SceneSerializer::SaveToFile(const Scene& scene,
                                  const std::filesystem::path& path) {
    const auto& reg = scene.Registry();

    // Assign stable serialization indices to every entity.
    std::unordered_map<entt::entity, int> entityIndex;
    {
        int idx = 0;
        reg.view<TagComponent>().each([&](entt::entity e, const TagComponent&) {
            entityIndex[e] = idx++;
        });
    }

    json root;
    root["version"] = 1;
    root["name"]    = scene.GetName();

    // World settings (global, not entity-bound)
    const WorldSettings& ws = scene.GetWorldSettings();
    if (ws.skyboxHdr.IsValid() || ws.sh9.IsValid() ||
        ws.prefilteredEnv.IsValid() || ws.brdfLut.IsValid() ||
        ws.skyboxCubemap.IsValid()) {
        json wj;
        if (ws.skyboxHdr.IsValid())       wj["skyboxHdr"]       = AssetToStr(ws.skyboxHdr);
        if (ws.sh9.IsValid())             wj["sh9"]             = AssetToStr(ws.sh9);
        if (ws.prefilteredEnv.IsValid())  wj["prefilteredEnv"]  = AssetToStr(ws.prefilteredEnv);
        if (ws.brdfLut.IsValid())         wj["brdfLut"]         = AssetToStr(ws.brdfLut);
        if (ws.skyboxCubemap.IsValid())   wj["skyboxCubemap"]   = AssetToStr(ws.skyboxCubemap);
        root["world"] = std::move(wj);
    }

    root["entities"] = json::array();

    reg.view<TagComponent>().each([&](entt::entity e, const TagComponent& tag) {
        json ej;
        ej["id"]  = entityIndex[e];
        ej["tag"] = tag.name;

        // Parent reference (serialization index, -1 = root)
        if (const auto* h = reg.try_get<HierarchyComponent>(e)) {
            auto it = entityIndex.find(h->parent);
            ej["parent"] = (it != entityIndex.end()) ? it->second : -1;
        } else {
            ej["parent"] = -1;
        }

        // Transform
        if (const auto* t = reg.try_get<TransformComponent>(e)) {
            ej["transform"] = {
                {"position", Vec3ToJson(t->position)},
                {"rotation", QuatToJson(t->rotation)},
                {"scale",    Vec3ToJson(t->scale)}
            };
        }

        // Camera
        if (const auto* c = reg.try_get<CameraComponent>(e)) {
            ej["camera"] = {
                {"fovY", c->fovY},
                {"near", c->nearPlane},
                {"far",  c->farPlane}
            };
        }
        if (reg.all_of<ActiveCameraTag>(e))
            ej["activeCamera"] = true;

        // Static mesh
        if (const auto* m = reg.try_get<StaticMeshComponent>(e)) {
            json mj;
            mj["mesh"]          = AssetToStr(m->meshAsset);
            mj["castShadow"]    = m->castShadow;
            mj["receiveShadow"] = m->receiveShadow;
            mj["materials"]     = json::array();
            for (const auto& slot : m->materialSlots)
                mj["materials"].push_back(AssetToStr(slot));
            ej["staticMesh"] = std::move(mj);
        }

        // Lights
        if (const auto* l = reg.try_get<DirectionalLightComponent>(e)) {
            ej["directionalLight"] = {
                {"color",      Vec3ToJson(l->color)},
                {"intensity",  l->intensity},
                {"castShadow", l->castShadow}
            };
        }
        if (const auto* l = reg.try_get<PointLightComponent>(e)) {
            ej["pointLight"] = {
                {"color",     Vec3ToJson(l->color)},
                {"intensity", l->intensity},
                {"range",     l->range}
            };
        }
        if (const auto* l = reg.try_get<SpotLightComponent>(e)) {
            ej["spotLight"] = {
                {"color",       Vec3ToJson(l->color)},
                {"intensity",   l->intensity},
                {"range",       l->range},
                {"innerAngle",  l->innerAngle},
                {"outerAngle",  l->outerAngle}
            };
        }

        if (reg.all_of<StaticGeometryTag>(e))
            ej["staticGeometry"] = true;

        root["entities"].push_back(std::move(ej));
    });

    std::ofstream f(path);
    if (!f) {
        SA_LOG_ERROR("SceneSerializer: cannot write '{}'", path.string());
        return false;
    }
    f << root.dump(2);
    SA_LOG_INFO("SceneSerializer: saved '{}' ({} entities)",
                path.string(), root["entities"].size());
    return f.good();
}

// ── Load ──────────────────────────────────────────────────────────────────────

bool SceneSerializer::LoadFromFile(Scene& scene,
                                    const std::filesystem::path& path) {
    std::ifstream f(path);
    if (!f) {
        SA_LOG_ERROR("SceneSerializer: cannot open '{}'", path.string());
        return false;
    }

    json root;
    try {
        f >> root;
    } catch (const json::exception& ex) {
        SA_LOG_ERROR("SceneSerializer: JSON parse error in '{}': {}",
                     path.string(), ex.what());
        return false;
    }

    if (root.value("version", 0) != 1) {
        SA_LOG_ERROR("SceneSerializer: unsupported version in '{}'", path.string());
        return false;
    }

    if (root.contains("name"))
        scene.SetName(root["name"].get<std::string>());

    // World settings
    if (root.contains("world")) {
        const auto& wj = root["world"];
        WorldSettings& ws = scene.GetWorldSettings();
        if (wj.contains("skyboxHdr"))      ws.skyboxHdr      = StrToAsset(wj["skyboxHdr"].get<std::string>());
        if (wj.contains("sh9"))            ws.sh9            = StrToAsset(wj["sh9"].get<std::string>());
        if (wj.contains("prefilteredEnv")) ws.prefilteredEnv = StrToAsset(wj["prefilteredEnv"].get<std::string>());
        if (wj.contains("brdfLut"))        ws.brdfLut        = StrToAsset(wj["brdfLut"].get<std::string>());
        if (wj.contains("skyboxCubemap"))  ws.skyboxCubemap  = StrToAsset(wj["skyboxCubemap"].get<std::string>());
    }

    const auto& entities = root["entities"];
    const size_t count   = entities.size();

    // Pass 1: create all entities, apply components (except hierarchy).
    std::vector<entt::entity> indexToEntity(count, entt::entity{entt::null});
    auto& reg = scene.Registry();

    for (size_t i = 0; i < count; ++i) {
        const json& ej = entities[i];

        const std::string name = ej.value("tag", "Entity");
        entt::entity e = scene.CreateEntity(name);
        indexToEntity[i] = e;

        // Transform
        if (ej.contains("transform")) {
            const auto& tj = ej["transform"];
            auto& t = reg.get<TransformComponent>(e);
            if (tj.contains("position")) t.position = JsonToVec3(tj["position"]);
            if (tj.contains("rotation")) t.rotation = JsonToQuat(tj["rotation"]);
            if (tj.contains("scale"))    t.scale    = JsonToVec3(tj["scale"]);
            reg.get<WorldTransformComponent>(e).dirty = true;
        }

        // Camera
        if (ej.contains("camera")) {
            const auto& cj = ej["camera"];
            CameraComponent c;
            c.fovY      = cj.value("fovY", c.fovY);
            c.nearPlane = cj.value("near", c.nearPlane);
            c.farPlane  = cj.value("far",  c.farPlane);
            reg.emplace<CameraComponent>(e, c);
        }
        if (ej.value("activeCamera", false))
            reg.emplace<ActiveCameraTag>(e);

        // Static mesh
        if (ej.contains("staticMesh")) {
            const auto& mj = ej["staticMesh"];
            StaticMeshComponent m;
            if (mj.contains("mesh"))
                m.meshAsset = StrToAsset(mj["mesh"].get<std::string>());
            if (mj.contains("materials"))
                for (const auto& slot : mj["materials"])
                    m.materialSlots.push_back(StrToAsset(slot.get<std::string>()));
            m.castShadow    = mj.value("castShadow",    m.castShadow);
            m.receiveShadow = mj.value("receiveShadow", m.receiveShadow);
            reg.emplace<StaticMeshComponent>(e, std::move(m));
        }

        // Lights
        if (ej.contains("directionalLight")) {
            const auto& lj = ej["directionalLight"];
            DirectionalLightComponent l;
            if (lj.contains("color")) l.color = JsonToVec3Color(lj["color"]);
            l.intensity  = lj.value("intensity",  l.intensity);
            l.castShadow = lj.value("castShadow", l.castShadow);
            reg.emplace<DirectionalLightComponent>(e, l);
        }
        if (ej.contains("pointLight")) {
            const auto& lj = ej["pointLight"];
            PointLightComponent l;
            if (lj.contains("color")) l.color = JsonToVec3Color(lj["color"]);
            l.intensity = lj.value("intensity", l.intensity);
            l.range     = lj.value("range",     l.range);
            reg.emplace<PointLightComponent>(e, l);
        }
        if (ej.contains("spotLight")) {
            const auto& lj = ej["spotLight"];
            SpotLightComponent l;
            if (lj.contains("color")) l.color = JsonToVec3Color(lj["color"]);
            l.intensity  = lj.value("intensity",  l.intensity);
            l.range      = lj.value("range",      l.range);
            l.innerAngle = lj.value("innerAngle", l.innerAngle);
            l.outerAngle = lj.value("outerAngle", l.outerAngle);
            reg.emplace<SpotLightComponent>(e, l);
        }

        if (ej.value("staticGeometry", false))
            reg.emplace<StaticGeometryTag>(e);
    }

    // Pass 2: wire up hierarchy (all entities exist now).
    for (size_t i = 0; i < count; ++i) {
        const int parentIdx = entities[i].value("parent", -1);
        if (parentIdx >= 0 && static_cast<size_t>(parentIdx) < count)
            scene.SetParent(indexToEntity[i], indexToEntity[parentIdx]);
    }

    SA_LOG_INFO("SceneSerializer: loaded '{}' ({} entities)", path.string(), count);
    return true;
}

} // namespace StellarAlia
