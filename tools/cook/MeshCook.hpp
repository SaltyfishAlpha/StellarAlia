#pragma once

#include "ImportScanner.hpp"
#include <filesystem>

namespace StellarAlia::Cook {

namespace fs = std::filesystem;

// Cooks a single mesh asset:
//   source (.gltf/.glb) + meta  →  outputDir/<uuid>.samesh
//
// All meshes/primitives in the source file are merged into a single vertex
// buffer and index buffer, with sub-mesh descriptors per primitive.
// Returns true on success.
bool CookMesh(const AssetEntry& entry, const fs::path& outputDir, bool force = false);

} // namespace StellarAlia::Cook
