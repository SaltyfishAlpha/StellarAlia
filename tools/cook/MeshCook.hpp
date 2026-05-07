#pragma once

#include "ImportScanner.hpp"
#include <filesystem>

namespace StellarAlia::Cook {

namespace fs = std::filesystem;

// Cooks a single mesh asset:
//   source (.gltf/.glb) + meta  →  outputDir/<uuid>.samesh    (monolithic, all nodes merged)
//                                   outputDir/<nodeId>.samesh  (one per static node, identity localTransform)
//                                   outputDir/<uuid>.sanode    (JSON manifest: node hierarchy + mesh IDs)
//
// Monolithic .samesh: all node primitives merged with baked world transforms.
//   Used for backward-compatible single-entity placement and for skinned meshes.
// Per-node .samesh: emitted only for non-skinned glTF; each file contains one
//   node's primitives with identity localTransform.  Entity TransformComponent
//   carries the node's local pose.  Node mesh IDs are derived via DeriveNodeMeshID().
// .sanode manifest: JSON listing all nodes (name, idx, parent_idx, mesh_id,
//   local_transform) — consumed by the import UI to spawn scene entities.
// Returns true on success.
bool CookMesh(const AssetEntry& entry, const fs::path& outputDir, bool force = false);

} // namespace StellarAlia::Cook
