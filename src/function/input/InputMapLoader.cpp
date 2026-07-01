#include "function/input/InputMapLoader.hpp"

#include "function/input/ActionMapJsonParser.hpp"
#include "function/input/InputSystem.hpp"
#include "core/logs/Log.hpp"
#include "core/io/FileIO.hpp"
#include "resource/MetaFile.hpp"

#include <filesystem>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <system_error>

namespace StellarAlia {

namespace fs = std::filesystem;

// Extract the canonical UUID string from a .sameta file via the shared parser
// (Issue #90). Returns empty string when missing or unreadable.
static std::string ReadUuidFromSameta(const fs::path& metaPath) {
    Import::MetaFile meta;
    if (!Import::MetaFile::Load(metaPath, meta)) return {};
    return meta.uuid.ToString();
}

static bool ReadFileToString(const fs::path& path, std::string& out) {
    auto s = IO::ReadText(path);
    if (!s) return false;
    out = std::move(*s);
    return true;
}

void InputMapLoader::LoadAll(const fs::path& projectDir,
                              const fs::path& cookCacheDir,
                              InputSystem&    inputSystem) {
    if (projectDir.empty()) return;

    const fs::path assetsDir = fs::is_directory(projectDir / "assets")
                                ? projectDir / "assets"
                                : projectDir;

    std::error_code ec;
    if (!fs::is_directory(assetsDir, ec)) return;

    std::vector<ActionMapDef> defs;
    // map "name" → first .sainputmap path that declared it. Used to warn when
    // two project files claim the same ActionMapDef.name — RegisterMaps would
    // silently let the later file win and flip e.g. passthrough/binding state.
    std::unordered_map<std::string, fs::path> nameSeenAt;

    for (const auto& de : fs::recursive_directory_iterator(
             assetsDir, fs::directory_options::skip_permission_denied, ec))
    {
        if (!de.is_regular_file(ec)) continue;
        const fs::path& meta = de.path();
        // Match files ending in ".sainputmap.sameta"
        if (meta.extension() != ".sameta") continue;
        const fs::path srcPath = meta.parent_path() / meta.stem(); // strip .sameta
        if (srcPath.extension() != ".sainputmap") continue;

        // Resolve payload: cooked copy first (shipped game), else live source (editor).
        std::string json;
        bool loaded = false;
        if (!cookCacheDir.empty()) {
            const std::string uuid = ReadUuidFromSameta(meta);
            if (!uuid.empty()) {
                const fs::path cooked = cookCacheDir / (uuid + ".sainputmap");
                if (fs::exists(cooked, ec) && ReadFileToString(cooked, json))
                    loaded = true;
            }
        }
        if (!loaded && fs::exists(srcPath, ec))
            loaded = ReadFileToString(srcPath, json);
        if (!loaded) {
            SA_LOG_WARN("InputMapLoader: could not read {}", srcPath.string());
            continue;
        }

        ActionMapDef def;
        if (!ActionMapJsonParser::Parse(json, def)) {
            SA_LOG_WARN("InputMapLoader: parse failed for {}", srcPath.string());
            continue;
        }
        auto inserted = nameSeenAt.try_emplace(def.name, srcPath);
        if (!inserted.second) {
            SA_LOG_WARN("InputMapLoader: '{}' declares ActionMapDef.name='{}' "
                        "already used by '{}' — RegisterMaps will let the later "
                        "file win (rename one to disambiguate)",
                        srcPath.string(), def.name,
                        inserted.first->second.string());
        }
        defs.push_back(std::move(def));
    }

    if (defs.empty()) return;

    const bool wasEmpty = inputSystem.IsMapStackEmpty();
    const std::string firstName = defs.front().name;
    inputSystem.RegisterMaps(std::move(defs));

    // Remember the first project map so EditorMode can push it automatically
    // on PIE entry (editor pops "Viewport" but isn't told what game map to push).
    inputSystem.SetDefaultGameMapName(firstName);

    // Establish a default context only when nothing was pushed yet —
    // editor pushes its own "Viewport"/"EditorGlobal" beforehand, which we
    // must not clobber.
    if (wasEmpty && !firstName.empty())
        inputSystem.PushMap(firstName);
}

} // namespace StellarAlia
