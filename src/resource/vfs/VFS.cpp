#include "resource/vfs/VFS.hpp"

namespace StellarAlia::Resource {

static std::filesystem::path s_engineCookCacheDir;
static std::filesystem::path s_projectCookCacheDir;

void VFS::SetEngineCookCacheDir(const fs::path& dir) {
    s_engineCookCacheDir = dir;
}

void VFS::SetCookCacheDir(const fs::path& dir) {
    s_projectCookCacheDir = dir;
}

const fs::path& VFS::GetCookCacheDir() {
    return s_projectCookCacheDir.empty() ? s_engineCookCacheDir : s_projectCookCacheDir;
}

std::optional<fs::path> VFS::ResolveCookedPath(const AssetID& id, std::string_view ext) {
    if (!id.IsValid()) return std::nullopt;

    const std::string filename = id.ToString() + std::string(ext);

    if (!s_projectCookCacheDir.empty()) {
        fs::path p = s_projectCookCacheDir / filename;
        if (fs::exists(p)) return p;
    }
    if (!s_engineCookCacheDir.empty()) {
        fs::path p = s_engineCookCacheDir / filename;
        if (fs::exists(p)) return p;
    }
    return std::nullopt;
}

} // namespace StellarAlia::Resource
