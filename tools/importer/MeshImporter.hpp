#pragma once

#include "importer/ImportScanner.hpp"
#include "core/asset/AssetID.hpp"
#include <filesystem>

namespace StellarAlia::Import {

namespace fs = std::filesystem;

// Cook a single .glb/.gltf asset:
//   - Cooks embedded images  → cookCacheDir/<id>.satex
//   - Cooks materials        → cookCacheDir/<id>.samat
//   - Generates .saskel + .saskel.sameta alongside the source file (one per skin)
//   - Cooks skeletons        → cookCacheDir/<id>.saskelc
//   - Generates .sanim + .sanim.sameta alongside the source file (one per animation)
//   - Cooks animations       → cookCacheDir/<id>.saanim
//   - Cooks monolithic mesh  → cookCacheDir/<id>.samesh
//   - Cooks per-node meshes  → cookCacheDir/<nodeId>.samesh  (static models only)
//   - Writes .sanode manifest→ cookCacheDir/<id>.sanode
//
// Returns true on success.
bool CookMesh(const AssetEntry& entry, const fs::path& cookCacheDir, bool force = false);

// Cook a single .sanim sidecar → cookCacheDir/<uuid>.saanim.
bool CookAnimSidecar(const AssetEntry& sanimEntry,
                     const fs::path&   sourceMeshPath,
                     const fs::path&   cookCacheDir,
                     bool              force = false);

// Cook a single .saskel sidecar → cookCacheDir/<uuid>.saskelc.
//
// Reads source_mesh + skin_index from the .saskel file, locates the source .glb
// via meshSourcePath, and extracts the requested skeleton.
bool CookSkeletonSidecar(const AssetEntry& sakelEntry,
                         const fs::path&   sourceMeshPath,
                         const fs::path&   cookCacheDir,
                         bool              force = false);

} // namespace StellarAlia::Import
