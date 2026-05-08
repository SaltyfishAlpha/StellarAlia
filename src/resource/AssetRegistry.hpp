#pragma once

#include "core/asset/AssetID.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace StellarAlia::Resource {

// ─────────────────────────────────────────────────────────────────────────────
// AssetEntry — one asset discovered from a .sameta sidecar file.
// ─────────────────────────────────────────────────────────────────────────────
struct AssetEntry {
    AssetID               id;
    std::string           name;       // source filename (e.g. "BoomBox.glb")
    std::string           type;       // "Mesh", "Texture", "Material", …
    std::filesystem::path sourcePath; // full absolute path to the source file
};

// ─────────────────────────────────────────────────────────────────────────────
// AssetRegistry
//
// Builds a UUID ↔ AssetEntry index by scanning .sameta sidecar files from one
// or two root directories (engine assets + project assets).
//
// Two use cases:
//   1. Editor UI   — FindByID() to show a human-readable name in the Inspector
//                    and EntriesByType() to populate the asset-picker popup.
//   2. Runtime (#5 step ③) — ResolveID() to convert a project-relative source
//                    path (e.g. "models/BoomBox.glb") to an AssetID without the
//                    caller needing to know the UUID.
// ─────────────────────────────────────────────────────────────────────────────
class AssetRegistry {
public:
    // Scan one or two directories for .sameta files (recursive).
    // Passing an empty path skips that directory.
    // Later call Scan() again after importing a new asset.
    void Scan(const std::filesystem::path& projectAssetsDir,
              const std::filesystem::path& engineAssetsDir = {});

    // ── Lookup by UUID ────────────────────────────────────────────────────────

    // Returns nullptr if not found.
    const AssetEntry* FindByID(const AssetID& id) const;

    // ── Lookup by path ────────────────────────────────────────────────────────

    // Resolve a project-relative source path to an AssetID.
    // "models/BoomBox.glb" → the UUID stored in BoomBox.glb.sameta.
    // Searches projectAssetsDir first, then engineAssetsDir.
    // Returns AssetID::Invalid() when not found.
    AssetID ResolveID(const std::filesystem::path& relPath) const;

    // Find an entry by its absolute source path.
    // Returns nullptr when not found.
    const AssetEntry* FindBySourcePath(const std::filesystem::path& absPath) const;

    // ── Filtered enumeration ──────────────────────────────────────────────────

    // All entries whose type field matches (case-sensitive).
    // Pass an empty string to receive every entry.
    std::vector<const AssetEntry*> EntriesByType(std::string_view filterType) const;

    size_t Count() const { return m_entries.size(); }
    const std::vector<AssetEntry>& All() const { return m_entries; }

private:
    void ScanDir(const std::filesystem::path& dir);

    std::vector<AssetEntry>              m_entries;
    std::unordered_map<uint64_t, size_t> m_idIndex;   // hi^lo  → m_entries index
    // path→index: keyed on canonical source path string hash
    std::unordered_map<std::size_t, size_t> m_pathIndex;

    std::filesystem::path m_projectAssetsDir;
    std::filesystem::path m_engineAssetsDir;
};

} // namespace StellarAlia::Resource
