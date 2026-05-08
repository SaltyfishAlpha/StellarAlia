#pragma once

#include "importer/ImportScanner.hpp"
#include <filesystem>

namespace StellarAlia::Import {

namespace fs = std::filesystem;

// Cook a single texture asset:
//   source (.png/.jpg/.hdr/…) + meta  →  cookCacheDir/<uuid>.satex
//
// Returns true on success.
bool CookTexture(const AssetEntry& entry, const fs::path& cookCacheDir, bool force = false);

} // namespace StellarAlia::Import
