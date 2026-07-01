#include "resource/MetaFile.hpp"

#include "core/io/FileIO.hpp"

#include <algorithm>
#include <sstream>
#include <vector>

namespace StellarAlia::Import {

bool MetaFile::GetBool(const std::string& key, bool def) const {
    auto it = settings.find(key);
    if (it == settings.end()) return def;
    return it->second == "1" || it->second == "true";
}

int MetaFile::GetInt(const std::string& key, int def) const {
    auto it = settings.find(key);
    if (it == settings.end()) return def;
    try { return std::stoi(it->second); } catch (...) { return def; }
}

std::string MetaFile::GetString(const std::string& key, const std::string& def) const {
    auto it = settings.find(key);
    return it != settings.end() ? it->second : def;
}

bool MetaFile::Load(const fs::path& metaPath, MetaFile& out) {
    const auto text = IO::ReadText(metaPath);
    if (!text) return false;

    out = {};
    std::istringstream in(*text);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key   = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);

        if      (key == "uuid") out.uuid = AssetID::FromString(value);
        else if (key == "type") out.type = value;
        else                    out.settings[key] = value;
    }
    return out.IsValid();
}

bool MetaFile::Save(const fs::path& metaPath, const MetaFile& meta) {
    if (metaPath.has_parent_path())
        IO::EnsureDir(metaPath.parent_path());

    std::string s = "# StellarAlia Asset Meta v1\n";
    s += "uuid=" + meta.uuid.ToString() + '\n';
    s += "type=" + meta.type + '\n';

    std::vector<std::pair<std::string, std::string>> sorted(
        meta.settings.begin(), meta.settings.end());
    std::sort(sorted.begin(), sorted.end());
    for (const auto& [k, v] : sorted)
        s += k + '=' + v + '\n';

    return IO::WriteText(metaPath, s);
}

} // namespace StellarAlia::Import
