    #include "resource/GltfSceneInstantiator.hpp"
#include "function/scene/Scene.hpp"
#include "function/scene/Components.hpp"
#include "resource/loaders/GltfLoader.hpp"
#include "resource/types/MeshData.hpp"
#include "resource/cook/CookedMesh.hpp"
#include "core/logs/Log.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace StellarAlia {

// Decompose a column-major mat4 (TRS, no shear) into position/rotation/scale.
static void DecomposeTRS(const glm::mat4& m,
                          glm::vec3&       outPos,
                          glm::quat&       outRot,
                          glm::vec3&       outScale)
{
    outPos = glm::vec3(m[3]);

    const glm::vec3 cx(m[0]);
    const glm::vec3 cy(m[1]);
    const glm::vec3 cz(m[2]);
    outScale = glm::vec3(glm::length(cx), glm::length(cy), glm::length(cz));

    // Guard against zero-scale degenerate matrices.
    if (outScale.x < 1e-7f) outScale.x = 1e-7f;
    if (outScale.y < 1e-7f) outScale.y = 1e-7f;
    if (outScale.z < 1e-7f) outScale.z = 1e-7f;

    const glm::mat3 rotMat(
        cx / outScale.x,
        cy / outScale.y,
        cz / outScale.z
    );
    outRot = glm::quat_cast(rotMat);
}

static void SpawnNode(Scene&                           scene,
                      entt::entity                     parent,
                      const Resource::SceneData&       data,
                      uint32_t                         nodeIdx,
                      const AssetID&                   fileId,
                      bool                             anySkinned)
{
    if (nodeIdx >= data.nodes.size()) return;
    const Resource::SceneNode& sn = data.nodes[nodeIdx];

    const std::string name = sn.name.empty()
        ? ("Node_" + std::to_string(nodeIdx)) : sn.name;

    entt::entity e = scene.CreateEntity(name);

    glm::vec3 pos; glm::quat rot; glm::vec3 scale;
    DecomposeTRS(sn.localTransform, pos, rot, scale);

    auto& tc    = scene.Registry().get<TransformComponent>(e);
    tc.position = pos;
    tc.rotation = rot;
    tc.scale    = scale;
    scene.MarkDirty(e);

    // Attach geometry for non-skinned nodes that reference a mesh.
    if (!anySkinned &&
        sn.meshIndex >= 0 &&
        sn.meshIndex < static_cast<int32_t>(data.meshes.size()))
    {
        StaticMeshComponent smc;
        smc.meshAsset = Resource::DeriveNodeMeshID(fileId, nodeIdx);
        // materialSlots left empty: renderer uses defaultMaterialID from .samesh
        scene.Registry().emplace<StaticMeshComponent>(e, std::move(smc));
    }

    if (parent != entt::null)
        scene.SetParent(e, parent);

    for (uint32_t ci : sn.children)
        SpawnNode(scene, e, data, ci, fileId, anySkinned);
}

bool GltfSceneInstantiator::Expand(Scene&             scene,
                                    entt::entity        parent,
                                    const std::string&  glbAbsPath,
                                    const AssetID&      fileId)
{
    auto sceneOpt = Resource::GltfLoader::Load(glbAbsPath);
    if (!sceneOpt) {
        SA_LOG_ERROR("GltfSceneInstantiator: cannot load '{}'", glbAbsPath);
        return false;
    }
    const Resource::SceneData& data = *sceneOpt;

    bool anySkinned = false;
    for (const auto& mesh : data.meshes)
        for (const auto& prim : mesh.primitives)
            if (!prim.skinVertices.empty()) { anySkinned = true; break; }

    for (uint32_t ri : data.rootNodes)
        SpawnNode(scene, parent, data, ri, fileId, anySkinned);

    SA_LOG_INFO("GltfSceneInstantiator: expanded '{}' ({} nodes{})",
                glbAbsPath, data.nodes.size(),
                anySkinned ? ", skinned — mesh split skipped" : "");
    return true;
}

} // namespace StellarAlia
