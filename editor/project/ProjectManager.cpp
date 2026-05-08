#include "project/ProjectManager.hpp"

#include "core/logs/Log.hpp"
#include "engine/SaProject.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace StellarAlia::Editor {

namespace fs = std::filesystem;

// ─── CreateProject ────────────────────────────────────────────────────────────

fs::path ProjectManager::CreateProject(const fs::path& parentDir,
                                        const std::string& name,
                                        ProjectTemplate tmpl,
                                        const fs::path& engineAssetsDir) {
    const fs::path projectRoot = parentDir / name;

    std::error_code ec;
    if (fs::exists(projectRoot, ec)) {
        SA_LOG_WARN("ProjectManager::CreateProject — directory already exists: '{}'",
                    projectRoot.string());
        return {};
    }

    // Create directory structure
    fs::create_directories(projectRoot / "assets" / "scenes",  ec);
    fs::create_directories(projectRoot / "assets" / "models",  ec);
    fs::create_directories(projectRoot / "assets" / "textures", ec);
    fs::create_directories(projectRoot / "assets" / "materials", ec);
    fs::create_directories(projectRoot / "cook_cache", ec);
    if (ec) {
        SA_LOG_WARN("ProjectManager::CreateProject — failed to create directories: {}",
                    ec.message());
        return {};
    }

    // .gitkeep in cook_cache so it's tracked by git without content
    {
        std::ofstream gitkeep(projectRoot / "cook_cache" / ".gitkeep");
    }

    // Write the startup scene
    const fs::path sceneDest = projectRoot / "assets" / "scenes" / "default.sascene";
    if (tmpl == ProjectTemplate::Default) {
        const fs::path sceneSrc = engineAssetsDir / "templates" / "scenes" / "default.sascene";
        if (fs::exists(sceneSrc, ec)) {
            fs::copy_file(sceneSrc, sceneDest, fs::copy_options::overwrite_existing, ec);
            if (ec)
                SA_LOG_WARN("ProjectManager::CreateProject — could not copy default scene: {}",
                            ec.message());
        }
    }
    if (!fs::exists(sceneDest, ec)) {
        // Fallback: write a minimal empty scene
        std::ofstream f(sceneDest);
        f << R"({"version":1,"name":"default","world":{},"entities":[]})" << '\n';
    }

    // Write .saproject
    SaProject proj;
    proj.name         = name;
    proj.version      = 1;
    proj.startupScene = "assets/scenes/default.sascene";

    const fs::path saprojectPath = projectRoot / (name + ".saproject");
    if (!SaveSaProject(saprojectPath, proj)) {
        SA_LOG_WARN("ProjectManager::CreateProject — failed to write .saproject");
        return {};
    }

    SA_LOG_INFO("ProjectManager: created project '{}' at '{}'", name, projectRoot.string());
    return saprojectPath;
}

// ─── Recent projects ──────────────────────────────────────────────────────────

void ProjectManager::LoadRecents(const fs::path& configPath) {
    m_recents.clear();
    std::ifstream f(configPath);
    if (!f) return;

    try {
        const auto j = nlohmann::json::parse(f);
        for (const auto& entry : j.value("recents", nlohmann::json::array())) {
            RecentProject rp;
            rp.name           = entry.value("name", "");
            rp.saprojectPath  = fs::path(entry.value("path", ""));
            if (!rp.saprojectPath.empty())
                m_recents.push_back(std::move(rp));
        }
    } catch (const nlohmann::json::exception& e) {
        SA_LOG_WARN("ProjectManager::LoadRecents — parse error: {}", e.what());
    }
}

void ProjectManager::SaveRecents(const fs::path& configPath) const {
    nlohmann::json j;
    auto arr = nlohmann::json::array();
    for (const auto& rp : m_recents) {
        nlohmann::json entry;
        entry["name"] = rp.name;
        entry["path"] = rp.saprojectPath.string();
        arr.push_back(std::move(entry));
    }
    j["recents"] = std::move(arr);

    std::ofstream f(configPath);
    if (!f) {
        SA_LOG_WARN("ProjectManager::SaveRecents — cannot write '{}'", configPath.string());
        return;
    }
    f << j.dump(2) << '\n';
}

void ProjectManager::AddRecent(const std::string& name, const fs::path& saprojectPath) {
    // Remove existing entry with same path
    m_recents.erase(std::remove_if(m_recents.begin(), m_recents.end(),
        [&](const RecentProject& rp){ return rp.saprojectPath == saprojectPath; }),
        m_recents.end());

    m_recents.insert(m_recents.begin(), RecentProject{ name, saprojectPath });

    if (static_cast<int>(m_recents.size()) > kMaxRecents)
        m_recents.resize(kMaxRecents);
}

void ProjectManager::RemoveRecent(const fs::path& saprojectPath) {
    m_recents.erase(std::remove_if(m_recents.begin(), m_recents.end(),
        [&](const RecentProject& rp){ return rp.saprojectPath == saprojectPath; }),
        m_recents.end());
}

void ProjectManager::RemoveStaleRecents() {
    std::error_code ec;
    m_recents.erase(std::remove_if(m_recents.begin(), m_recents.end(),
        [&](const RecentProject& rp){ return !fs::exists(rp.saprojectPath, ec); }),
        m_recents.end());
}

} // namespace StellarAlia::Editor
