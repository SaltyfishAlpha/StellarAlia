#include "engine/SaProject.hpp"

#include "core/logs/Log.hpp"

#include <nlohmann/json.hpp>

#include <fstream>

namespace StellarAlia {

bool LoadSaProject(const std::filesystem::path& path, SaProject& out) {
    std::ifstream f(path);
    if (!f) {
        SA_LOG_WARN("SaProject: cannot open '{}'", path.string());
        return false;
    }
    try {
        const auto j = nlohmann::json::parse(f);
        out.name         = j.value("name",         "Unnamed");
        out.version      = j.value("version",      1);
        out.startupScene = j.value("startupScene", "");
    } catch (const nlohmann::json::exception& e) {
        SA_LOG_WARN("SaProject: parse error in '{}': {}", path.string(), e.what());
        return false;
    }
    return true;
}

bool SaveSaProject(const std::filesystem::path& path, const SaProject& proj) {
    nlohmann::json j;
    j["name"]         = proj.name;
    j["version"]      = proj.version;
    j["startupScene"] = proj.startupScene;

    std::ofstream f(path);
    if (!f) {
        SA_LOG_WARN("SaProject: cannot write '{}'", path.string());
        return false;
    }
    f << j.dump(2) << '\n';
    return true;
}

} // namespace StellarAlia
