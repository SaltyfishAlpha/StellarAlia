#pragma once

#include "core/asset/AssetID.hpp"
#include "resource/types/MeshData.hpp"

#include <filesystem>
#include <functional>

namespace StellarAlia::Cook {

namespace fs = std::filesystem;

// Derive a deterministic material AssetID from a parent mesh UUID and the
// material's index within the glTF file.  Different Fibonacci constants from
// DeriveImageID (in MeshCook.cpp) to guarantee no collisions.
AssetID DeriveMaterialID(const AssetID& meshId, int32_t matIndex);

// Write a .samat JSON file for one glTF material into outputDir.
//   matID         — pre-computed UUID (from DeriveMaterialID)
//   resolveTexID  — maps a glTF imageIndex → cooked texture AssetID (pass
//                   DeriveImageID from MeshCook as a lambda)
//
// Returns true on success.  Skips silently if the file already exists (unless
// the caller wants to force re-cook — simply delete the file beforehand).
bool CookMaterial(const Resource::MaterialData& mat,
                  const AssetID& matID,
                  const std::function<AssetID(int32_t imageIndex)>& resolveTexID,
                  const fs::path& outputDir);

} // namespace StellarAlia::Cook
