#include "resource/EntityTemplateRegistry.hpp"

#include <algorithm>

namespace fs = std::filesystem;

namespace StellarAlia::Editor {

void EntityTemplateRegistry::Scan(const fs::path& engineAssetsDir) {
    m_entries.clear();
    m_defaultScene.clear();

    const fs::path entitiesDir = engineAssetsDir / "templates" / "entities";
    const fs::path scenesDir   = engineAssetsDir / "templates" / "scenes";

    const fs::path candidate = scenesDir / "default.sascene";
    if (fs::exists(candidate))
        m_defaultScene = fs::absolute(candidate);

    if (!fs::exists(entitiesDir)) return;

    std::error_code ec;

    // Top-level .sascene files (no category).
    for (const auto& entry : fs::directory_iterator(entitiesDir, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".sascene") continue;
        m_entries.push_back({ "", entry.path().stem().string(), entry.path() });
    }

    // One level of subdirectories — subdirectory name becomes the category.
    for (const auto& dirEntry : fs::directory_iterator(entitiesDir, ec)) {
        if (!dirEntry.is_directory()) continue;
        std::string category = dirEntry.path().filename().string();
        for (const auto& fileEntry : fs::directory_iterator(dirEntry.path(), ec)) {
            if (!fileEntry.is_regular_file()) continue;
            if (fileEntry.path().extension() != ".sascene") continue;
            m_entries.push_back({
                std::move(category),
                fileEntry.path().stem().string(),
                fileEntry.path()
            });
            // category was moved — re-assign for subsequent iterations
            category = dirEntry.path().filename().string();
        }
    }

    // Sort: top-level entries first, then by category, then by label.
    std::sort(m_entries.begin(), m_entries.end(),
        [](const TemplateEntry& a, const TemplateEntry& b) {
            if (a.category != b.category) {
                if (a.category.empty()) return true;
                if (b.category.empty()) return false;
                return a.category < b.category;
            }
            return a.label < b.label;
        });
}

} // namespace StellarAlia::Editor
