#include "engine/SaProject.hpp"

#include "core/logs/Log.hpp"
#include "core/io/FileIO.hpp"

#include <nlohmann/json.hpp>

namespace StellarAlia {

bool LoadSaProject(const std::filesystem::path& path, SaProject& out) {
    nlohmann::json j;
    if (!IO::ReadJson(path, j)) return false;
    out.name         = j.value("name",         "Unnamed");
    out.version      = j.value("version",      1);
    out.startupScene = j.value("startupScene", "");
    return true;
}

bool SaveSaProject(const std::filesystem::path& path, const SaProject& proj) {
    nlohmann::json j;
    j["name"]         = proj.name;
    j["version"]      = proj.version;
    j["startupScene"] = proj.startupScene;
    return IO::WriteJson(path, j);
}

} // namespace StellarAlia
