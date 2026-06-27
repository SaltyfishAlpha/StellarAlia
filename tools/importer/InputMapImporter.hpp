#pragma once

#include "importer/ImportScanner.hpp"
#include <filesystem>

namespace StellarAlia::Import {

namespace fs = std::filesystem;

// Cook a single .sainputmap asset:
//   source (.sainputmap JSON) + meta  →  cookCacheDir/<uuid>.sainputmap
//
// Structural validation only (must contain "name" and "actions" array);
// the cooked output is the validated JSON bytes (no binary transform).
//
// Returns true on success.
bool CookInputMap(const AssetEntry& entry, const fs::path& cookCacheDir, bool force = false);

} // namespace StellarAlia::Import
