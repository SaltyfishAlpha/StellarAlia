#pragma once

#include "core/asset/AssetID.hpp"
#include <filesystem>
#include <string>
#include <unordered_map>

namespace StellarAlia::Cook {

namespace fs = std::filesystem;

// Represents the contents of a .sameta sidecar file.
//
// On-disk format (plain text, key=value, '#' starts a comment):
//   # StellarAlia Asset Meta v1
//   uuid=550e8400-e29b-41d4-a716-446655440000
//   type=Texture
//   srgb=1
//   mipmaps=1
//
struct MetaFile {
    AssetID                                  uuid;
    std::string                              type;    // "Texture", "Mesh"
    std::unordered_map<std::string, std::string> settings;

    bool IsValid() const { return uuid.IsValid() && !type.empty(); }

    // Convenience helpers for settings.
    bool        GetBool  (const std::string& key, bool   def = false) const;
    int         GetInt   (const std::string& key, int    def = 0)     const;
    std::string GetString(const std::string& key, const std::string& def = {}) const;

    static bool Load(const fs::path& metaPath, MetaFile& out);
    static bool Save(const fs::path& metaPath, const MetaFile& meta);

    // Returns the path of the .sameta sidecar for a given source file.
    static fs::path MetaPathFor(const fs::path& sourcePath) {
        return fs::path(sourcePath.string() + ".sameta");
    }
};

} // namespace StellarAlia::Cook
