#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace StellarAlia::Editor {

enum class ProjectTemplate {
    Empty,    // assets/ tree + blank scene + .saproject
    Default,  // Empty + copy engine templates/scenes/default.sascene as startup scene
};

struct RecentProject {
    std::string           name;
    std::filesystem::path saprojectPath;
};

// ─────────────────────────────────────────────────────────────────────────────
// ProjectManager — create/open projects and persist a recent-projects list.
// ─────────────────────────────────────────────────────────────────────────────
class ProjectManager {
public:
    // Create a new project directory tree at parentDir/name.
    // Returns the absolute path to the created .saproject file, or empty on failure.
    static std::filesystem::path CreateProject(
        const std::filesystem::path& parentDir,
        const std::string& name,
        ProjectTemplate tmpl,
        const std::filesystem::path& engineAssetsDir);

    void LoadRecents(const std::filesystem::path& configPath);
    void SaveRecents(const std::filesystem::path& configPath) const;

    // Prepend an entry (or move it to front if already present).
    // Capped at 10 entries.
    void AddRecent(const std::string& name, const std::filesystem::path& saprojectPath);

    // Remove entries whose .saproject path no longer exists on disk.
    void RemoveStaleRecents();

    const std::vector<RecentProject>& GetRecents() const { return m_recents; }
    void RemoveRecent(const std::filesystem::path& saprojectPath);

private:
    std::vector<RecentProject> m_recents;

    static constexpr int kMaxRecents = 10;
};

} // namespace StellarAlia::Editor
