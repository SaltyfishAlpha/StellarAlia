#include "resource/config_manager/ConfigManager.hpp"

#include "core/logs/Log.hpp"

#include <fstream>
#include <vector>

namespace StellarAlia::Resource::ConfigManager {
namespace {

ConfigData g_config{};
bool g_loaded = false;

std::string Trim(const std::string& input) {
    const auto start = input.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = input.find_last_not_of(" \t\r\n");
    return input.substr(start, end - start + 1);
}

void ParseConfigStream(std::istream& stream, ConfigData& cfg) {
    std::string line;
    while (std::getline(stream, line)) {
        const auto commentPos = line.find_first_of("#;");
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
        }

        line = Trim(line);
        if (line.empty()) {
            continue;
        }

        const auto equalsPos = line.find('=');
        if (equalsPos == std::string::npos) {
            continue;
        }

        const std::string key = Trim(line.substr(0, equalsPos));
        const std::string value = Trim(line.substr(equalsPos + 1));

        if (key == "application_name") {
            cfg.applicationName = value;
        } else if (key == "engine_name") {
            cfg.engineName = value;
        } else if (key == "window_title") {
            cfg.windowTitle = value;
        }
    }
}

bool TryLoadFromPath(const std::filesystem::path& path, ConfigData& cfg) {
    if (path.empty()) {
        return false;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    SA_LOG_INFO("Loading config from {}", path.string());
    ParseConfigStream(file, cfg);
    return true;
}

void EnsureDerivedDefaults(ConfigData& cfg) {
    if (cfg.windowTitle.empty()) {
        cfg.windowTitle = cfg.applicationName;
    }
    if (cfg.engineName.empty()) {
        cfg.engineName = cfg.applicationName;
    }
}

}  // namespace

void Load(const std::filesystem::path& customPath) {
    ConfigData cfg{};
    bool loaded = false;

    std::vector<std::filesystem::path> candidates;
    if (!customPath.empty()) {
        candidates.push_back(customPath);
    }
    candidates.emplace_back("config/app.ini");
    candidates.emplace_back("../config/app.ini");

    for (const auto& candidate : candidates) {
        if (TryLoadFromPath(candidate, cfg)) {
            loaded = true;
            break;
        }
    }

    if (!loaded) {
        SA_LOG_INFO("Config file not found; using built-in defaults");
    }

    EnsureDerivedDefaults(cfg);
    g_config = cfg;
    g_loaded = true;
}

const ConfigData& Get() {
    if (!g_loaded) {
        Load();
    }
    return g_config;
}

}  // namespace StellarAlia::Resource::ConfigManager

