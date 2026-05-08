#include "resource/AssetRegistry.hpp"

#include <algorithm>
#include <fstream>
#include <functional>
#include <string>

namespace StellarAlia::Resource {

namespace fs = std::filesystem;

// ─── .sameta parser ──────────────────────────────────────────────────────────
// Mirrors tools/cook/MetaFile format — kept local here to avoid a link
// dependency on the cook tools.

static bool ParseSameta(const fs::path& metaPath, AssetID& outID, std::string& outType) {
    std::ifstream f(metaPath);
    if (!f) return false;

    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key   = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);

        if      (key == "uuid") outID   = AssetID::FromString(value);
        else if (key == "type") outType = value;
    }
    return outID.IsValid() && !outType.empty();
}

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
    for (const auto& entry : fs::recursive_directory_iterator(
             dir, fs::directory_options::skip_permission_denied, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().extension() != ".sameta") continue;

        // Source path = meta path minus ".sameta" suffix.
        std::string srcStr = entry.path().string();
        srcStr.resize(srcStr.size() - std::string_view(".sameta").size());
        const fs::path srcPath(srcStr);
        if (!fs::exists(srcPath)) continue;

        AssetID     id;
        std::string type;
        if (!ParseSameta(entry.path(), id, type)) continue;

        const uint64_t idKey = id.hi ^ id.lo;
        if (m_idIndex.count(idKey)) continue; // deduplicate (project wins over engine)

        const std::size_t pathKey = std::hash<std::string>{}(
            fs::weakly_canonical(srcPath).string());

        const size_t idx = m_entries.size();
        m_idIndex[idKey]   = idx;
        m_pathIndex[pathKey] = idx;
        m_entries.push_back({ id, srcPath.filename().string(), type, srcPath });
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
