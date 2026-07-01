#include "resource/AssetRegistry.hpp"
#include "resource/MetaFile.hpp"

#include <algorithm>
#include <functional>
#include <string>

namespace StellarAlia::Resource {

namespace fs = std::filesystem;

// ─── AssetRegistry ────────────────────────────────────────────────────────────

void AssetRegistry::Scan(const fs::path& projectAssetsDir,
                         const fs::path& engineAssetsDir) {
    m_entries.clear();
    m_idIndex.clear();
    m_pathIndex.clear();
    m_projectAssetsDir = projectAssetsDir;
    m_engineAssetsDir  = engineAssetsDir;

    if (!projectAssetsDir.empty()) ScanDir(projectAssetsDir);
    if (!engineAssetsDir.empty())  ScanDir(engineAssetsDir);
}

void AssetRegistry::ScanDir(const fs::path& dir) {
    if (!fs::exists(dir) || !fs::is_directory(dir)) return;

    std::error_code ec;
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        // Skip the "templates" directory — template assets are scaffolding for
        // new-file creation, not assignable assets.
        if (it->is_directory(ec) && it->path().filename() == "templates") {
            it.disable_recursion_pending();
            continue;
        }
        const auto& entry = *it;
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().extension() != ".sameta") continue;

        // Source path = meta path minus ".sameta" suffix.
        std::string srcStr = entry.path().string();
        srcStr.resize(srcStr.size() - std::string_view(".sameta").size());
        const fs::path srcPath(srcStr);
        if (!fs::exists(srcPath)) continue;

        Import::MetaFile meta;
        if (!Import::MetaFile::Load(entry.path(), meta)) continue;

        const uint64_t idKey = meta.uuid.hi ^ meta.uuid.lo;
        if (m_idIndex.count(idKey)) continue; // deduplicate (project wins over engine)

        const std::size_t pathKey = std::hash<std::string>{}(
            fs::weakly_canonical(srcPath).string());

        const size_t idx = m_entries.size();
        m_idIndex[idKey]   = idx;
        m_pathIndex[pathKey] = idx;
        m_entries.push_back({ meta.uuid, srcPath.filename().string(), meta.type, srcPath });
    }
}

const AssetEntry* AssetRegistry::FindByID(const AssetID& id) const {
    if (!id.IsValid()) return nullptr;
    const auto it = m_idIndex.find(id.hi ^ id.lo);
    if (it == m_idIndex.end()) return nullptr;
    return &m_entries[it->second];
}

AssetID AssetRegistry::ResolveID(const fs::path& relPath) const {
    // Try project dir first, then engine dir.
    for (const auto* base : { &m_projectAssetsDir, &m_engineAssetsDir }) {
        if (base->empty()) continue;
        std::error_code ec;
        const fs::path full = fs::weakly_canonical(*base / relPath, ec);
        if (ec || !fs::exists(full)) continue;

        const std::size_t key = std::hash<std::string>{}(full.string());
        const auto it = m_pathIndex.find(key);
        if (it != m_pathIndex.end())
            return m_entries[it->second].id;
    }
    return AssetID::Invalid();
}

const AssetEntry* AssetRegistry::FindBySourcePath(const fs::path& absPath) const {
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(absPath, ec);
    if (ec) return nullptr;
    const std::size_t key = std::hash<std::string>{}(canonical.string());
    const auto it = m_pathIndex.find(key);
    if (it == m_pathIndex.end()) return nullptr;
    return &m_entries[it->second];
}

std::vector<const AssetEntry*> AssetRegistry::EntriesByType(std::string_view filterType) const {
    std::vector<const AssetEntry*> out;
    for (const auto& e : m_entries) {
        if (filterType.empty() || e.type == filterType)
            out.push_back(&e);
    }
    std::sort(out.begin(), out.end(),
              [](const AssetEntry* a, const AssetEntry* b) {
                  return a->name < b->name;
              });
    return out;
}

} // namespace StellarAlia::Resource
