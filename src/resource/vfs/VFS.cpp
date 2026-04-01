#include "resource/vfs/VFS.hpp"

namespace StellarAlia::Resource {

static std::filesystem::path s_cookCacheDir;

void VFS::SetCookCacheDir(const fs::path& dir) {
    s_cookCacheDir = dir;
}

const fs::path& VFS::GetCookCacheDir() {
    return s_cookCacheDir;
}

std::optional<fs::path> VFS::ResolveCookedPath(const AssetID& id, std::string_view ext) {
    if (!id.IsValid() || s_cookCacheDir.empty()) return std::nullopt;

    fs::path p = s_cookCacheDir / (id.ToString() + std::string(ext));
    if (!fs::exists(p)) return std::nullopt;
    return p;
}

} // namespace StellarAlia::Resource
