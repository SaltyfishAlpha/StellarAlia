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
//       "areaLight":         { "color": [1,1,1], "intensity": 8.0,
//                              "width": 2.0, "height": 3.0, "twoSided": false },
//       "skybox": { "cubemap": "uuid..." },
//       "ibl":    { "irradiance": "uuid...", "prefilteredEnv": "uuid...", "brdfLut": "uuid..." },
//       "materialOverride": {
//           "materialAsset": "uuid...",          // optional: replaces mesh-default material
//           "scalars":  { "roughnessFactor": 0.3, "emissiveFactor": [1,0.5,0] },
//           "textures": { "t_BaseColor": "uuid..." }
//       },
//       // (Backward compat: "pbrSurface" + "materialParams" are migrated on load)
//       "staticGeometry": true
//     }
//   ]
// }

#include "function/scene/SceneSerializer.hpp"
#include "function/scene/Scene.hpp"
#include "core/logs/Log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <queue>
#include <type_traits>
#include <unordered_map>

namespace StellarAlia {

using json = nlohmann::json;

// ── JSON helpers ──────────────────────────────────────────────────────────────

static json Vec3ToJson(const glm::vec3& v) { return { v.x, v.y, v.z }; }
static json QuatToJson(const glm::quat& q) { return { q.w, q.x, q.y, q.z }; }

static glm::vec3 JsonToVec3(const json& j) {
    return { j[0].get<float>(), j[1].get<float>(), j[2].get<float>() };
}
static glm::vec4 JsonToVec4(const json& j) {
    return { j[0].get<float>(), j[1].get<float>(),
             j[2].get<float>(), j[3].get<float>() };
}
static glm::vec3 JsonToVec3Color(const json& j) { return JsonToVec3(j); }
static glm::quat JsonToQuat(const json& j) {
    return { j[0].get<float>(), j[1].get<float>(),
             j[2].get<float>(), j[3].get<float>() };  // w, x, y, z
}

static std::string AssetToStr(const AssetID& id) { return id.ToString(); }
static AssetID     StrToAsset(const std::string& s) { return AssetID::FromString(s); }

// ── SerializeToJson ───────────────────────────────────────────────────────────

nlohmann::json SceneSerializer::SerializeToJson(const Scene& scene) {
    const auto& reg = scene.Registry();

    // BFS from root order → stable, order-preserving indices.
    std::vector<entt::entity> saveOrder;
    {
        std::queue<entt::entity> q;
        for (entt::entity root : scene.GetRootOrder())
            if (reg.valid(root)) q.push(root);
        while (!q.empty()) {
            entt::entity e = q.front(); q.pop();
            saveOrder.push_back(e);
            if (const auto* h = reg.try_get<HierarchyComponent>(e))
                for (entt::entity child : h->children)
                    if (reg.valid(child)) q.push(child);
        }
    }

    std::unordered_map<entt::entity, int> entityIndex;
    for (int idx = 0; idx < static_cast<int>(saveOrder.size()); ++idx)
        entityIndex[saveOrder[idx]] = idx;

    json root;
    root["version"] = 1;
    root["name"]    = scene.GetName();

    // World settings (global, not entity-bound)
    {
        const WorldSettings& ws = scene.GetWorldSettings();
        json wj;
        wj["backgroundMode"]  = (ws.backgroundMode == WorldSettings::BackgroundMode::Skybox)
                                 ? "Skybox" : "SolidColor";
        wj["backgroundColor"] = Vec3ToJson(ws.backgroundColor);
        if (ws.skyboxHdr.IsValid())       wj["skyboxHdr"]       = AssetToStr(ws.skyboxHdr);
        if (ws.sh9.IsValid())             wj["sh9"]             = AssetToStr(ws.sh9);
        if (ws.prefilteredEnv.IsValid())  wj["prefilteredEnv"]  = AssetToStr(ws.prefilteredEnv);
        if (ws.brdfLut.IsValid())         wj["brdfLut"]         = AssetToStr(ws.brdfLut);
        if (ws.skyboxCubemap.IsValid())   wj["skyboxCubemap"]   = AssetToStr(ws.skyboxCubemap);
        {
            const PostProcessSettings& pp = ws.pp;
            json ppj;
            ppj["bloomEnabled"]   = pp.bloomEnabled;
            ppj["bloomThreshold"] = pp.bloomThreshold;
            ppj["bloomStrength"]  = pp.bloomStrength;
            ppj["bloomRadius"]    = pp.bloomRadius;
            ppj["bloomMipLevels"] = pp.bloomMipLevels;
            ppj["tonemapMode"]    = (pp.tonemapMode == PostProcessSettings::TonemapMode::LUT) ? "LUT" : "Builtin";
            if (pp.tonemapLut.IsValid()) ppj["tonemapLut"] = AssetToStr(pp.tonemapLut);
            ppj["exposure"]         = pp.exposure;
            ppj["lutStrength"]      = pp.lutStrength;
            {
                const ColorGradingSettings& cg = pp.colorGrading;
                json cgj;
                cgj["enabled"]    = cg.enabled;
                cgj["lift"]       = Vec3ToJson(cg.lift);
                cgj["midtone"]    = Vec3ToJson(cg.midtone);
                cgj["gain"]       = Vec3ToJson(cg.gain);
                cgj["saturation"] = cg.saturation;
                cgj["contrast"]   = cg.contrast;
                ppj["colorGrading"] = std::move(cgj);
            }
            ppj["ssaoEnabled"]      = pp.ssaoEnabled;
            ppj["ssaoRadius"]       = pp.ssaoRadius;
            ppj["ssaoStrength"]     = pp.ssaoStrength;
            ppj["ssaoBias"]         = pp.ssaoBias;
            ppj["ssaoDirections"]   = pp.ssaoDirections;
            ppj["ssaoSteps"]        = pp.ssaoSteps;
            ppj["ssaoBlurSharpness"]= pp.ssaoBlurSharpness;
            ppj["taaEnabled"]       = pp.taaEnabled;
            ppj["taaBlendStatic"]   = pp.taaBlendStatic;
            ppj["taaBlendMotion"]   = pp.taaBlendMotion;
            ppj["taaAntiGhosting"]  = pp.taaAntiGhosting;
            ppj["autoExposureEnabled"] = pp.autoExposureEnabled;
            ppj["aeEvMin"]          = pp.aeEvMin;
            ppj["aeEvMax"]          = pp.aeEvMax;
            ppj["aeAdaptSpeed"]     = pp.aeAdaptSpeed;
            ppj["aeLowPercent"]     = pp.aeLowPercent;
            ppj["aeHighPercent"]    = pp.aeHighPercent;
            ppj["dofEnabled"]       = pp.dofEnabled;
            ppj["focusDistance"]    = pp.focusDistance;
            ppj["aperture"]         = pp.aperture;
            ppj["focalLength"]      = pp.focalLength;
            ppj["dofSamples"]       = pp.dofSamples;
            ppj["maxCocPx"]         = pp.maxCocPx;
            wj["postProcess"]       = std::move(ppj);
        }
        root["world"] = std::move(wj);
    }

    root["entities"] = json::array();

    for (entt::entity e : saveOrder) {
        const TagComponent& tag = reg.get<TagComponent>(e);
        json ej;
        ej["id"]  = entityIndex[e];
        ej["tag"] = tag.name;
        if (const auto* eid = reg.try_get<EntityIdComponent>(e))
            ej["scene_local_id"] = eid->sceneLocalId;

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
                {"fovY",     c->fovY},
                {"near",     c->nearPlane},
                {"far",      c->farPlane},
                {"priority", c->priority}
            };
        }

        // Static mesh
        if (const auto* m = reg.try_get<StaticMeshComponent>(e))
            ej["staticMesh"] = { {"mesh", AssetToStr(m->meshAsset)} };

        // Skinned mesh
        if (const auto* m = reg.try_get<SkinnedMeshComponent>(e))
            ej["skinnedMesh"] = { {"mesh", AssetToStr(m->meshAsset)} };

        // Mesh renderer (shared config for static + skinned)
        if (const auto* mr = reg.try_get<MeshRendererComponent>(e)) {
            json mrj;
            mrj["castShadow"]    = mr->castShadow;
            mrj["receiveShadow"] = mr->receiveShadow;
            mrj["materials"]     = json::array();
            for (const auto& slot : mr->materialSlots)
                mrj["materials"].push_back(AssetToStr(slot));
            ej["meshRenderer"] = std::move(mrj);
        }

        // Animator
        if (const auto* a = reg.try_get<AnimatorComponent>(e)) {
            ej["animator"] = {
                {"clip",    AssetToStr(a->clipAsset)},
                {"speed",   a->speed},
                {"looping", a->looping}
            };
        }

        // Rigid body
        if (const auto* rb = reg.try_get<RigidBodyComponent>(e)) {
            const char* typeStr = (rb->type == RigidBodyComponent::Type::Static)    ? "static"
                                : (rb->type == RigidBodyComponent::Type::Kinematic) ? "kinematic"
                                                                                     : "dynamic";
            ej["rigidBody"] = {
                {"type",        typeStr},
                {"mass",        rb->mass},
                {"friction",    rb->friction},
                {"restitution", rb->restitution}
            };
        }

        // Collider
        if (const auto* col = reg.try_get<ColliderComponent>(e)) {
            const char* shapeStr = (col->shape == ColliderComponent::Shape::Sphere)  ? "sphere"
                                 : (col->shape == ColliderComponent::Shape::Capsule) ? "capsule"
                                                                                     : "box";
            ej["collider"] = {
                {"shape",    shapeStr},
                {"extents",  Vec3ToJson(col->extents)},
                {"offset",   Vec3ToJson(col->offset)},
                {"rotation", QuatToJson(col->rotation)}
            };
        }

        // Script
        if (const auto* sc = reg.try_get<ScriptComponent>(e)) {
            json scriptJson = {
                {"asset_id", sc->scriptId.ToString()},
                {"class",    sc->className},
            };
            if (!sc->fields.empty()) {
                json fieldsArr = json::array();
                for (const auto& [name, value] : sc->fields) {
                    json entry;
                    entry["name"] = name;
                    std::visit([&entry](auto&& v) {
                        using T = std::decay_t<decltype(v)>;
                        if constexpr (std::is_same_v<T, bool>) {
                            entry["kind"]  = "Bool";
                            entry["value"] = v;
                        } else if constexpr (std::is_same_v<T, int32_t>) {
                            // Int32 covers Enum too — JSON readers disambiguate
                            // via the kind string written here only as "Int32";
                            // Enum's identity comes from the live schema, not JSON.
                            entry["kind"]  = "Int32";
                            entry["value"] = v;
                        } else if constexpr (std::is_same_v<T, float>) {
                            entry["kind"]  = "Float";
                            entry["value"] = v;
                        } else if constexpr (std::is_same_v<T, std::string>) {
                            entry["kind"]  = "String";
                            entry["value"] = v;
                        } else if constexpr (std::is_same_v<T, glm::vec2>) {
                            entry["kind"]  = "Vec2";
                            entry["value"] = { v.x, v.y };
                        } else if constexpr (std::is_same_v<T, glm::vec3>) {
                            entry["kind"]  = "Vec3";
                            entry["value"] = { v.x, v.y, v.z };
                        } else if constexpr (std::is_same_v<T, glm::vec4>) {
                            entry["kind"]  = "Vec4";
                            entry["value"] = { v.x, v.y, v.z, v.w };
                        } else if constexpr (std::is_same_v<T, AssetID>) {
                            entry["kind"]  = "AssetRef";
                            entry["value"] = v.ToString();
                        } else if constexpr (std::is_same_v<T, uint64_t>) {
                            entry["kind"]  = "EntityRef";
                            entry["value"] = v;
                        }
                    }, value);
                    fieldsArr.push_back(std::move(entry));
                }
                scriptJson["fields"] = std::move(fieldsArr);
            }
            ej["script"] = std::move(scriptJson);
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
        if (const auto* l = reg.try_get<AreaLightComponent>(e)) {
            ej["areaLight"] = {
                {"color",         Vec3ToJson(l->color)},
                {"intensity",     l->intensity},
                {"width",         l->size.x},
                {"height",        l->size.y},
                {"twoSided",      l->twoSided},
                {"emissiveScale", l->emissiveScale}
            };
        }

        // MaterialOverrideComponent
        if (const auto* mo = reg.try_get<MaterialOverrideComponent>(e)) {
            json moj;
            if (mo->materialAsset.IsValid())
                moj["materialAsset"] = AssetToStr(mo->materialAsset);
            json scalarsJ = json::object();
            for (const auto& [name, val] : mo->scalars) {
                std::visit([&](const auto& v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, float>)
                        scalarsJ[name] = v;
                    else if constexpr (std::is_same_v<T, glm::vec2>)
                        scalarsJ[name] = { v.x, v.y };
                    else if constexpr (std::is_same_v<T, glm::vec3>)
                        scalarsJ[name] = { v.x, v.y, v.z };
                    else if constexpr (std::is_same_v<T, glm::vec4>)
                        scalarsJ[name] = { v.x, v.y, v.z, v.w };
                }, val);
            }
            moj["scalars"] = std::move(scalarsJ);
            json texturesJ = json::object();
            for (const auto& [name, id] : mo->textures)
                if (id.IsValid()) texturesJ[name] = AssetToStr(id);
            moj["textures"] = std::move(texturesJ);
            ej["materialOverride"] = std::move(moj);
        }

        if (reg.all_of<StaticGeometryTag>(e))
            ej["staticGeometry"] = true;

        root["entities"].push_back(std::move(ej));
    }

    return root;
}

// ── Save ──────────────────────────────────────────────────────────────────────

bool SceneSerializer::SaveToFile(const Scene& scene,
                                  const std::filesystem::path& path) {
    const json root = SerializeToJson(scene);
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

// ── DeserializeFromJson ───────────────────────────────────────────────────────

bool SceneSerializer::DeserializeFromJson(Scene& scene, const nlohmann::json& root) {
    if (root.value("version", 0) != 1) {
        SA_LOG_ERROR("SceneSerializer: unsupported schema version");
        return false;
    }

    if (root.contains("name"))
        scene.SetName(root["name"].get<std::string>());

    // World settings
    if (root.contains("world")) {
        const auto& wj = root["world"];
        WorldSettings& ws = scene.GetWorldSettings();

        if (wj.value("backgroundMode", "SolidColor") == "Skybox")
            ws.backgroundMode = WorldSettings::BackgroundMode::Skybox;
        else
            ws.backgroundMode = WorldSettings::BackgroundMode::SolidColor;
        if (wj.contains("backgroundColor")) ws.backgroundColor = JsonToVec3(wj["backgroundColor"]);

        if (wj.contains("skyboxHdr"))      ws.skyboxHdr      = StrToAsset(wj["skyboxHdr"].get<std::string>());
        if (wj.contains("sh9"))            ws.sh9            = StrToAsset(wj["sh9"].get<std::string>());
        if (wj.contains("prefilteredEnv")) ws.prefilteredEnv = StrToAsset(wj["prefilteredEnv"].get<std::string>());
        if (wj.contains("brdfLut"))        ws.brdfLut        = StrToAsset(wj["brdfLut"].get<std::string>());
        if (wj.contains("skyboxCubemap"))  ws.skyboxCubemap  = StrToAsset(wj["skyboxCubemap"].get<std::string>());

        PostProcessSettings& pp = ws.pp;
        if (wj.contains("postProcess")) {
            const auto& ppj = wj["postProcess"];
            pp.bloomEnabled   = ppj.value("bloomEnabled",   pp.bloomEnabled);
            pp.bloomThreshold = ppj.value("bloomThreshold", pp.bloomThreshold);
            pp.bloomStrength  = ppj.value("bloomStrength",  pp.bloomStrength);
            pp.bloomRadius    = ppj.value("bloomRadius",    pp.bloomRadius);
            pp.bloomMipLevels = ppj.value("bloomMipLevels", pp.bloomMipLevels);
            pp.tonemapMode    = (ppj.value("tonemapMode", "Builtin") == "LUT")
                                    ? PostProcessSettings::TonemapMode::LUT
                                    : PostProcessSettings::TonemapMode::Builtin;
            if (ppj.contains("tonemapLut")) pp.tonemapLut = StrToAsset(ppj["tonemapLut"].get<std::string>());
            pp.exposure         = ppj.value("exposure",         pp.exposure);
            pp.lutStrength      = ppj.value("lutStrength",      pp.lutStrength);
            if (ppj.contains("colorGrading")) {
                const auto& cgj = ppj["colorGrading"];
                ColorGradingSettings& cg = pp.colorGrading;
                cg.enabled    = cgj.value("enabled",    cg.enabled);
                if (cgj.contains("lift"))    cg.lift    = JsonToVec3(cgj["lift"]);
                if (cgj.contains("midtone")) cg.midtone = JsonToVec3(cgj["midtone"]);
                if (cgj.contains("gain"))    cg.gain    = JsonToVec3(cgj["gain"]);
                cg.saturation = cgj.value("saturation", cg.saturation);
                cg.contrast   = cgj.value("contrast",   cg.contrast);
            }
            pp.ssaoEnabled      = ppj.value("ssaoEnabled",      pp.ssaoEnabled);
            pp.ssaoRadius       = ppj.value("ssaoRadius",       pp.ssaoRadius);
            pp.ssaoStrength     = ppj.value("ssaoStrength",     pp.ssaoStrength);
            pp.ssaoBias         = ppj.value("ssaoBias",         pp.ssaoBias);
            pp.ssaoDirections   = ppj.value("ssaoDirections",   pp.ssaoDirections);
            pp.ssaoSteps        = ppj.value("ssaoSteps",        pp.ssaoSteps);
            pp.ssaoBlurSharpness= ppj.value("ssaoBlurSharpness",pp.ssaoBlurSharpness);
            pp.taaEnabled       = ppj.value("taaEnabled",       pp.taaEnabled);
            pp.taaBlendStatic   = ppj.value("taaBlendStatic",   pp.taaBlendStatic);
            pp.taaBlendMotion   = ppj.value("taaBlendMotion",   pp.taaBlendMotion);
            pp.taaAntiGhosting  = ppj.value("taaAntiGhosting",  pp.taaAntiGhosting);
            pp.autoExposureEnabled = ppj.value("autoExposureEnabled", pp.autoExposureEnabled);
            pp.aeEvMin          = ppj.value("aeEvMin",          pp.aeEvMin);
            pp.aeEvMax          = ppj.value("aeEvMax",          pp.aeEvMax);
            pp.aeAdaptSpeed     = ppj.value("aeAdaptSpeed",     pp.aeAdaptSpeed);
            pp.aeLowPercent     = ppj.value("aeLowPercent",     pp.aeLowPercent);
            pp.aeHighPercent    = ppj.value("aeHighPercent",    pp.aeHighPercent);
            pp.dofEnabled       = ppj.value("dofEnabled",       pp.dofEnabled);
            pp.focusDistance    = ppj.value("focusDistance",    pp.focusDistance);
            pp.aperture         = ppj.value("aperture",         pp.aperture);
            pp.focalLength      = ppj.value("focalLength",      pp.focalLength);
            pp.dofSamples       = ppj.value("dofSamples",       pp.dofSamples);
            pp.maxCocPx         = ppj.value("maxCocPx",         pp.maxCocPx);
        } else {
            // Backward compat: read old top-level tonemap keys from pre-#40 scenes.
            pp.tonemapMode = (wj.value("tonemapMode", "Builtin") == "LUT")
                                 ? PostProcessSettings::TonemapMode::LUT
                                 : PostProcessSettings::TonemapMode::Builtin;
            if (wj.contains("tonemapLut")) pp.tonemapLut = StrToAsset(wj["tonemapLut"].get<std::string>());
            pp.exposure    = wj.value("exposure",    pp.exposure);
            pp.lutStrength = wj.value("lutStrength", pp.lutStrength);
        }
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

        // Restore sceneLocalId from disk so script-field EntityRef references
        // resolve. Missing key (pre-#75 scene) → keep auto-assigned ID.
        if (ej.contains("scene_local_id"))
            scene.AssignSceneLocalId(e, ej["scene_local_id"].get<uint64_t>());

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
            c.nearPlane = cj.value("near",     c.nearPlane);
            c.farPlane  = cj.value("far",      c.farPlane);
            c.priority  = cj.value("priority", c.priority);
            // Backward compat: old scenes used "activeCamera":true at entity level.
            if (ej.value("activeCamera", false) && c.priority == 0)
                c.priority = 1;
            reg.emplace<CameraComponent>(e, c);
        }

        // Static mesh
        if (ej.contains("staticMesh")) {
            const auto& mj = ej["staticMesh"];
            StaticMeshComponent m;
            if (mj.contains("mesh"))
                m.meshAsset = StrToAsset(mj["mesh"].get<std::string>());
            reg.emplace<StaticMeshComponent>(e, std::move(m));
            // Backward compat: old scenes stored materials/shadows inside staticMesh.
            if (mj.contains("materials") || mj.contains("castShadow") || mj.contains("receiveShadow")) {
                MeshRendererComponent mr;
                if (mj.contains("materials"))
                    for (const auto& slot : mj["materials"])
                        mr.materialSlots.push_back(StrToAsset(slot.get<std::string>()));
                mr.castShadow    = mj.value("castShadow",    mr.castShadow);
                mr.receiveShadow = mj.value("receiveShadow", mr.receiveShadow);
                reg.emplace_or_replace<MeshRendererComponent>(e, std::move(mr));
            }
        }

        // Skinned mesh
        if (ej.contains("skinnedMesh")) {
            const auto& mj = ej["skinnedMesh"];
            auto& smc = reg.emplace<SkinnedMeshComponent>(e);
            if (mj.contains("mesh"))
                smc.meshAsset = StrToAsset(mj["mesh"].get<std::string>());
            // Backward compat: old scenes stored materials inside skinnedMesh.
            if (mj.contains("materials")) {
                MeshRendererComponent mr;
                for (const auto& slot : mj["materials"])
                    mr.materialSlots.push_back(StrToAsset(slot.get<std::string>()));
                reg.emplace_or_replace<MeshRendererComponent>(e, std::move(mr));
            }
        }

        // Mesh renderer (new canonical location for materials + shadow flags)
        if (ej.contains("meshRenderer")) {
            const auto& mrj = ej["meshRenderer"];
            MeshRendererComponent mr;
            if (mrj.contains("materials"))
                for (const auto& slot : mrj["materials"])
                    mr.materialSlots.push_back(StrToAsset(slot.get<std::string>()));
            mr.castShadow    = mrj.value("castShadow",    mr.castShadow);
            mr.receiveShadow = mrj.value("receiveShadow", mr.receiveShadow);
            reg.emplace_or_replace<MeshRendererComponent>(e, std::move(mr));
        }

        // "skeleton" key silently ignored — SkeletonComponent removed; ID is
        // now derived from SkinnedMeshComponent::meshAsset at runtime.

        // Animator
        if (ej.contains("animator")) {
            const auto& aj = ej["animator"];
            AnimatorComponent a;
            if (aj.contains("clip"))
                a.clipAsset = StrToAsset(aj["clip"].get<std::string>());
            a.speed   = aj.value("speed",   a.speed);
            a.looping = aj.value("looping", a.looping);
            reg.emplace<AnimatorComponent>(e, a);
        }

        // Rigid body
        if (ej.contains("rigidBody")) {
            const auto& rj = ej["rigidBody"];
            RigidBodyComponent rb;
            const std::string typeStr = rj.value("type", "dynamic");
            if      (typeStr == "static")    rb.type = RigidBodyComponent::Type::Static;
            else if (typeStr == "kinematic") rb.type = RigidBodyComponent::Type::Kinematic;
            else                             rb.type = RigidBodyComponent::Type::Dynamic;
            rb.mass        = rj.value("mass",        rb.mass);
            rb.friction    = rj.value("friction",    rb.friction);
            rb.restitution = rj.value("restitution", rb.restitution);
            reg.emplace<RigidBodyComponent>(e, rb);
        }

        // Collider
        if (ej.contains("collider")) {
            const auto& cj = ej["collider"];
            ColliderComponent col;
            const std::string shapeStr = cj.value("shape", "box");
            if      (shapeStr == "sphere")  col.shape = ColliderComponent::Shape::Sphere;
            else if (shapeStr == "capsule") col.shape = ColliderComponent::Shape::Capsule;
            else                            col.shape = ColliderComponent::Shape::Box;
            if (cj.contains("extents"))  col.extents  = JsonToVec3(cj["extents"]);
            if (cj.contains("offset"))   col.offset   = JsonToVec3(cj["offset"]);
            if (cj.contains("rotation")) col.rotation = JsonToQuat(cj["rotation"]);
            reg.emplace<ColliderComponent>(e, col);
        }

        // Script
        if (ej.contains("script")) {
            const auto& sj = ej["script"];
            ScriptComponent sc;
            sc.className = sj.value("class", std::string{});
            if (sj.contains("asset_id")) {
                sc.scriptId = AssetID::FromString(sj.value("asset_id", std::string{}));
            } else if (sj.contains("path")) {
                // Pre-#73 legacy: serializer no longer has AssetRegistry access here.
                // Recover class name from the path stem; user must re-drop the .cs to
                // restore the scriptId (.cs.sameta UUID).
                const std::string p = sj.value("path", std::string{});
                if (sc.className.empty() && !p.empty())
                    sc.className = std::filesystem::path(p).stem().string();
            }
            // Per-entity field values (#75). Kind comes from JSON, not the live
            // schema — schema may not be available at load time (ALC not yet
            // compiled). RecompileEditing's migration step will reconcile.
            if (sj.contains("fields") && sj["fields"].is_array()) {
                for (const auto& f : sj["fields"]) {
                    if (!f.contains("name") || !f.contains("kind") || !f.contains("value")) continue;
                    const std::string name = f["name"].get<std::string>();
                    const std::string kindStr = f["kind"].get<std::string>();
                    const auto kind = ScriptFieldKindFromString(kindStr);
                    const auto& v = f["value"];
                    ScriptFieldValue val;
                    bool ok = true;
                    switch (kind) {
                        case ScriptFieldKind::Bool:      val = v.get<bool>(); break;
                        case ScriptFieldKind::Int32:
                        case ScriptFieldKind::Enum:      val = v.get<int32_t>(); break;
                        case ScriptFieldKind::Float:     val = v.get<float>(); break;
                        case ScriptFieldKind::String:    val = v.get<std::string>(); break;
                        case ScriptFieldKind::Vec2:
                            if (v.is_array() && v.size() >= 2)
                                val = glm::vec2{v[0].get<float>(), v[1].get<float>()};
                            else ok = false;
                            break;
                        case ScriptFieldKind::Vec3:
                            if (v.is_array() && v.size() >= 3)
                                val = glm::vec3{v[0].get<float>(), v[1].get<float>(), v[2].get<float>()};
                            else ok = false;
                            break;
                        case ScriptFieldKind::Vec4:
                            if (v.is_array() && v.size() >= 4)
                                val = glm::vec4{v[0].get<float>(), v[1].get<float>(),
                                                 v[2].get<float>(), v[3].get<float>()};
                            else ok = false;
                            break;
                        case ScriptFieldKind::Color:
                            if (v.is_array() && v.size() == 3)
                                val = glm::vec3{v[0].get<float>(), v[1].get<float>(), v[2].get<float>()};
                            else if (v.is_array() && v.size() == 4)
                                val = glm::vec4{v[0].get<float>(), v[1].get<float>(),
                                                 v[2].get<float>(), v[3].get<float>()};
                            else ok = false;
                            break;
                        case ScriptFieldKind::AssetRef:  val = AssetID::FromString(v.get<std::string>()); break;
                        case ScriptFieldKind::EntityRef: val = v.get<uint64_t>(); break;
                        default: ok = false; break;
                    }
                    if (ok) sc.fields.emplace(std::move(name), std::move(val));
                }
            }
            reg.emplace<ScriptComponent>(e, sc);
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
        if (ej.contains("areaLight")) {
            const auto& lj = ej["areaLight"];
            AreaLightComponent l;
            if (lj.contains("color")) l.color = JsonToVec3Color(lj["color"]);
            l.intensity      = lj.value("intensity",     l.intensity);
            l.size.x         = lj.value("width",         l.size.x);
            l.size.y         = lj.value("height",        l.size.y);
            l.twoSided       = lj.value("twoSided",      l.twoSided);
            l.emissiveScale  = lj.value("emissiveScale", l.emissiveScale);
            reg.emplace<AreaLightComponent>(e, l);
        }

        // MaterialOverrideComponent (new format)
        if (ej.contains("materialOverride")) {
            const auto& moj = ej["materialOverride"];
            MaterialOverrideComponent mo;
            if (moj.contains("materialAsset"))
                mo.materialAsset = StrToAsset(moj["materialAsset"].get<std::string>());
            if (moj.contains("scalars")) {
                for (const auto& [name, val] : moj["scalars"].items()) {
                    if (val.is_number())
                        mo.scalars[name] = val.get<float>();
                    else if (val.is_array()) {
                        const size_t n = val.size();
                        if      (n == 2) mo.scalars[name] = glm::vec2{val[0].get<float>(), val[1].get<float>()};
                        else if (n == 3) mo.scalars[name] = JsonToVec3(val);
                        else if (n == 4) mo.scalars[name] = JsonToVec4(val);
                    }
                }
            }
            if (moj.contains("textures")) {
                for (const auto& [name, val] : moj["textures"].items()) {
                    const AssetID id = StrToAsset(val.get<std::string>());
                    if (id.IsValid()) mo.textures[name] = id;
                }
            }
            reg.emplace<MaterialOverrideComponent>(e, std::move(mo));
        }
        // Backward compat: old pbrSurface / materialParams → MaterialOverrideComponent
        else if (ej.contains("pbrSurface") || ej.contains("materialParams")) {
            auto& mo = reg.emplace_or_replace<MaterialOverrideComponent>(e);
            if (ej.contains("pbrSurface")) {
                const auto& pbj = ej["pbrSurface"];
                if (pbj.contains("baseColor"))
                    mo.scalars["baseColorFactor"] = JsonToVec4(pbj["baseColor"]);
                mo.scalars["roughnessFactor"] = pbj.value("roughness", 0.5f);
                mo.scalars["metallicFactor"]  = pbj.value("metallic",  0.f);
                if (pbj.contains("albedoMap"))
                    mo.textures["t_BaseColor"] = StrToAsset(pbj["albedoMap"].get<std::string>());
                if (pbj.contains("normalMap"))
                    mo.textures["t_Normal"]    = StrToAsset(pbj["normalMap"].get<std::string>());
            }
            if (ej.contains("materialParams")) {
                const auto& mpj = ej["materialParams"];
                if (mpj.contains("scalars")) {
                    for (const auto& [name, val] : mpj["scalars"].items()) {
                        if (val.is_number())
                            mo.scalars[name] = val.get<float>();
                        else if (val.is_array()) {
                            const size_t n = val.size();
                            if      (n == 2) mo.scalars[name] = glm::vec2{val[0].get<float>(), val[1].get<float>()};
                            else if (n == 3) mo.scalars[name] = JsonToVec3(val);
                            else if (n == 4) mo.scalars[name] = JsonToVec4(val);
                        }
                    }
                }
                if (mpj.contains("textures")) {
                    for (const auto& [name, val] : mpj["textures"].items()) {
                        const AssetID id = StrToAsset(val.get<std::string>());
                        if (id.IsValid()) mo.textures[name] = id;
                    }
                }
            }
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

    return true;
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

    if (!DeserializeFromJson(scene, root)) return false;
    SA_LOG_INFO("SceneSerializer: loaded '{}' ({} entities)",
                path.string(), root.value("entities", json::array()).size());
    return true;
}

// ── SpawnFromTemplate ──────────────────────────────────────────────────────────

std::vector<entt::entity> SceneSerializer::SpawnFromTemplate(
    Scene& scene, const std::filesystem::path& path)
{
    // Snapshot state that templates must not overwrite.
    const WorldSettings savedWS   = scene.GetWorldSettings();
    const std::string   savedName = scene.GetName();

    // Collect entity IDs that exist before the load so we can identify new ones.
    std::vector<entt::entity> before;
    scene.Registry().view<TagComponent>().each(
        [&](entt::entity e, const TagComponent&) { before.push_back(e); });
    std::sort(before.begin(), before.end());

    if (!LoadFromFile(scene, path)) {
        scene.GetWorldSettings() = savedWS;
        scene.SetName(savedName);
        return {};
    }

    // Restore global state overwritten by LoadFromFile.
    scene.GetWorldSettings() = savedWS;
    scene.SetName(savedName);

    // Return newly added root entities (no HierarchyComponent parent).
    std::vector<entt::entity> added;
    scene.Registry().view<TagComponent>().each([&](entt::entity e, const TagComponent&) {
        if (std::binary_search(before.begin(), before.end(), e)) return;
        const auto* hc = scene.Registry().try_get<HierarchyComponent>(e);
        if (!hc || hc->parent == entt::null)
            added.push_back(e);
    });
    return added;
}

} // namespace StellarAlia
