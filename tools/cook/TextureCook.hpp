#pragma once

#include "ImportScanner.hpp"
#include <filesystem>

namespace StellarAlia::Cook {

namespace fs = std::filesystem;

// Cooks a single texture asset:
//   source (.png/.jpg/.hdr/…) + meta  →  outputDir/<uuid>.satex
//
// Returns true on success.
// If the output file already exists and is newer than the source + meta,
// cooking is skipped (incremental build).
bool CookTexture(const AssetEntry& entry, const fs::path& outputDir, bool force = false);

} // namespace StellarAlia::Cook
