#include "importer/ImportScanner.hpp"

#include <algorithm>
#include <iostream>

namespace StellarAlia::Import {

std::string AssetTypeFromExtension(const fs::path& ext) {
    std::string e = ext.string();
    std::transform(e.begin(), e.end(), e.begin(), ::tolower);

    if (e == ".png"  || e == ".jpg" || e == ".jpeg" ||
        e == ".bmp"  || e == ".tga" || e == ".hdr")
        return "Texture";

    if (e == ".gltf" || e == ".glb")
        return "Mesh";

    if (e == ".mat")
        return "Material";

    // .sanim / .saskel / .sameta are sidecar files, not primary cook targets.
    return {};
}

static void ApplyDefaultSettings(MetaFile& meta, const fs::path& sourcePath) {
    if (meta.type == "Texture") {
        const bool isHdr = sourcePath.extension().string() == ".hdr";
        const bool inHdri = [&] {
            for (auto& part : sourcePath)
                if (part.string() == "hdri") return true;
            return false;
        }();
        meta.settings["srgb"]    = (isHdr || inHdri) ? "0" : "1";
        meta.settings["mipmaps"] = "1";
    } else if (meta.type == "Mesh") {
        meta.settings["merge_submeshes"] = "0";
    }
}

std::vector<AssetEntry> ScanAndImport(const fs::path& dir) {
    std::vector<AssetEntry> results;

    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        std::cerr << "[Import] Directory not found: " << dir << '\n';
        return results;
    }

    for (auto& entry : fs::recursive_directory_iterator(
             dir, fs::directory_options::skip_permission_denied))
    {
        if (!entry.is_regular_file()) continue;

        const fs::path& src = entry.path();
        const std::string ext = src.extension().string();

        // Skip sidecars and hidden files.
        if (ext == ".sameta" || ext == ".sanim" || ext == ".saskel" ||
            src.filename().string().front() == '.')
            continue;

        const std::string type = AssetTypeFromExtension(src.extension());
        if (type.empty()) continue;

        const fs::path metaPath = MetaFile::MetaPathFor(src);

        AssetEntry ae;
        ae.sourcePath = src;
        ae.metaPath   = metaPath;

        if (fs::exists(metaPath)) {
            if (!MetaFile::Load(metaPath, ae.meta)) {
                std::cerr << "[Import] Failed to load existing meta: " << metaPath << '\n';
                continue;
            }
        } else {
            ae.meta.uuid = AssetID::Generate();
            ae.meta.type = type;
            ApplyDefaultSettings(ae.meta, src);

            if (!MetaFile::Save(metaPath, ae.meta)) {
                std::cerr << "[Import] Failed to write meta: " << metaPath << '\n';
                continue;
            }
            std::cout << "[Import] " << src.filename() << "  →  "
                      << ae.meta.uuid.ToString() << '\n';
        }

        results.push_back(std::move(ae));
    }

    return results;
}

} // namespace StellarAlia::Import
