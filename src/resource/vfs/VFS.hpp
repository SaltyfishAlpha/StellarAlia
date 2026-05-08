#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "core/asset/AssetID.hpp"

namespace StellarAlia::Resource {

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// VFS — Virtual File System
//
// Thin lookup layer over the cook cache directory.
// Maps (AssetID, extension) → absolute path to the cooked file.
//
// Usage:
//   VFS::SetCookCacheDir("build/cook_cache");
//   auto path = VFS::ResolveCookedPath(id, ".satex");
// ─────────────────────────────────────────────────────────────────────────────
class VFS {
public:
    // Set the engine cook cache — fixed at startup, never changes between projects.
    // Contains cooked engine built-in assets (meshes, textures, materials).
    static void SetEngineCookCacheDir(const fs::path& dir);

    // Set the active project's cook cache — updated when the user switches projects.
    // Contains cooked assets imported into the current project.
    // Checked before the engine cache in ResolveCookedPath.
    static void SetCookCacheDir(const fs::path& dir);

    // Returns the project cook cache (or the engine cache if no project is loaded).
    static const fs::path& GetCookCacheDir();

    // Resolve an AssetID to a cooked file path.
    // Checks the project cache first, then falls back to the engine cache.
    // Returns std::nullopt if the file is not found in either location.
    // ext must include the leading dot, e.g. ".satex", ".samesh".
    [[nodiscard]] static std::optional<fs::path>
    ResolveCookedPath(const AssetID& id, std::string_view ext);
};

} // namespace StellarAlia::Resource
