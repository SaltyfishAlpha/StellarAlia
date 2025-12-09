#pragma once

#include <filesystem>
#include <string>

namespace StellarAlia::Resource::ConfigManager {

struct ConfigData {
    std::string applicationName = "StellarAlia-Renderer";
    std::string engineName = "StellarAlia";
    std::string windowTitle = "StellarAlia";
};

void Load(const std::filesystem::path& customPath = {});
const ConfigData& Get();

}  // namespace StellarAlia::Resource::ConfigManager

