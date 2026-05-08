#pragma once

#include "core/asset/AssetID.hpp"
#include "resource/types/MeshData.hpp"

#include <filesystem>
#include <functional>

namespace StellarAlia::Import {

namespace fs = std::filesystem;

// Derive a deterministic material AssetID from a parent mesh UUID and index.
AssetID DeriveMaterialID(const AssetID& meshId, int32_t matIndex);

// Write a .samat JSON file for one glTF material.
bool CookMaterial(const Resource::MaterialData& mat,
                  const AssetID& matID,
                  const std::function<AssetID(int32_t imageIndex)>& resolveTexID,
                  const fs::path& cookCacheDir);

// Cook a standalone .mat source file → cookCacheDir/<id>.samat.
bool CookStandaloneMaterial(const fs::path& sourcePath,
                             const AssetID&  id,
                             const fs::path& cookCacheDir,
                             bool            force);

} // namespace StellarAlia::Import
