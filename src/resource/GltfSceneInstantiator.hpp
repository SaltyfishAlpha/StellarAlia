#pragma once

#include "core/asset/AssetID.hpp"
#include <entt/entt.hpp>
#include <string>

namespace StellarAlia {

class Scene;

// ─────────────────────────────────────────────────────────────────────────────
// GltfSceneInstantiator
//
// Expands a glTF/GLB file into a hierarchy of scene entities, one per node.
// Nodes with geometry receive a StaticMeshComponent whose meshAsset is derived
// via DeriveNodeMeshID(fileId, nodeIndex) — matching what MeshCook produces for
// the per-node .samesh files.
//
// Usage (e.g. from the asset import UI):
//
//   GltfSceneInstantiator::Expand(scene, parentEntity, "/abs/path/Model.glb", fileUUID);
//
// All spawned entities become children of 'parent' (which may be entt::null
// to place them at scene root).  Entity transforms use the glTF node's LOCAL
// transform so they compose correctly with TransformComponent hierarchy.
//
// Skinned glTF files (those with skin/joint data) are not split per-node —
// call Expand and then attach a SkinnedMeshComponent on the root entity.
// ─────────────────────────────────────────────────────────────────────────────
class GltfSceneInstantiator {
public:
    // Loads 'glbAbsPath', walks the node tree, and creates entities in 'scene'
    // as children of 'parent'.  'fileId' is the .sameta UUID for 'glbAbsPath'.
    // Returns true on success (even partial — some nodes may lack cooked meshes).
    static bool Expand(Scene& scene, entt::entity parent,
                       const std::string& glbAbsPath,
                       const AssetID& fileId);
};

} // namespace StellarAlia
