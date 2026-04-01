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
    // Set the root cook cache directory (called once at startup from ResourceManager).
    static void SetCookCacheDir(const fs::path& dir);

    // Returns the cook cache dir set via SetCookCacheDir.
    static const fs::path& GetCookCacheDir();

    // Resolve an AssetID to a cooked file path.
    // Returns std::nullopt if the file does not exist.
    // ext must include the leading dot, e.g. ".satex", ".samesh".
    [[nodiscard]] static std::optional<fs::path>
    ResolveCookedPath(const AssetID& id, std::string_view ext);
};

} // namespace StellarAlia::Resource
