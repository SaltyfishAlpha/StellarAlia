#pragma once

#include "MetaFile.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace StellarAlia::Cook {

namespace fs = std::filesystem;

// One discovered asset (source file + its .sameta).
struct AssetEntry {
    fs::path sourcePath;
    fs::path metaPath;
    MetaFile meta;
};

// Determines the asset type string from a file extension.
// Returns "" for unrecognised extensions (not a cookable asset).
std::string AssetTypeFromExtension(const fs::path& ext);

// Recursively scans `dir`, generates a .sameta for every cookable file that
// does not already have one, and returns all discovered assets.
// Existing .sameta files are never modified — UUIDs are stable.
std::vector<AssetEntry> ScanAndImport(const fs::path& dir);

} // namespace StellarAlia::Cook
