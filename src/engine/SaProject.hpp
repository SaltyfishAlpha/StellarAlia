#pragma once

#include <filesystem>
#include <string>

namespace StellarAlia {

// ─────────────────────────────────────────────────────────────────────────────
// SaProject — in-memory representation of a .saproject file.
//
// .saproject lives at the project root (next to the assets/ folder).
// Format: JSON, UTF-8.
// ─────────────────────────────────────────────────────────────────────────────
struct SaProject {
    std::string name;
    int         version      = 1;
    std::string startupScene; // relative to project root, e.g. "assets/scenes/foo.sascene"
};

// Read a .saproject JSON file into out. Returns false and logs on failure.
bool LoadSaProject(const std::filesystem::path& path, SaProject& out);

// Write a SaProject to a .saproject JSON file. Returns false and logs on failure.
bool SaveSaProject(const std::filesystem::path& path, const SaProject& proj);

} // namespace StellarAlia
