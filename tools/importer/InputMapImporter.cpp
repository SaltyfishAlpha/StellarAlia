#include "importer/InputMapImporter.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <sstream>

namespace StellarAlia::Import {

static bool NeedsRecook(const AssetEntry& entry, const fs::path& outPath) {
    if (!fs::exists(outPath)) return true;
    const auto outTime = fs::last_write_time(outPath);
    if (fs::last_write_time(entry.sourcePath) > outTime) return true;
    if (fs::exists(entry.metaPath) && fs::last_write_time(entry.metaPath) > outTime) return true;
    return false;
}

bool CookInputMap(const AssetEntry& entry, const fs::path& cookCacheDir, bool force) {
    fs::create_directories(cookCacheDir);

    const fs::path outPath = cookCacheDir / (entry.meta.uuid.ToString() + ".sainputmap");

    if (!force && !NeedsRecook(entry, outPath)) {
        std::cout << "[Cook] SKIP (up-to-date)  " << entry.sourcePath.filename() << '\n';
        return true;
    }

    std::ifstream in(entry.sourcePath);
    if (!in.is_open()) {
        std::cerr << "[Cook] FAIL  " << entry.sourcePath.filename()
                  << " — could not open source\n";
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string src = ss.str();

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(src);
    } catch (const std::exception& e) {
        std::cerr << "[Cook] FAIL  " << entry.sourcePath.filename()
                  << " — JSON parse error: " << e.what() << '\n';
        return false;
    }

    if (!j.contains("name") || !j["name"].is_string()) {
        std::cerr << "[Cook] FAIL  " << entry.sourcePath.filename()
                  << " — missing required string field \"name\"\n";
        return false;
    }
    if (!j.contains("actions") || !j["actions"].is_array()) {
        std::cerr << "[Cook] FAIL  " << entry.sourcePath.filename()
                  << " — missing required array field \"actions\"\n";
        return false;
    }

    std::ofstream out(outPath, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "[Cook] FAIL  " << entry.sourcePath.filename()
                  << " — could not open output: " << outPath << '\n';
        return false;
    }
    out << j.dump(2);

    std::cout << "[Cook] IM    " << entry.sourcePath.filename()
              << "  →  " << outPath.filename() << '\n';
    return true;
}

} // namespace StellarAlia::Import
