#include "ImportScanner.hpp"

#include <algorithm>
#include <iostream>

namespace StellarAlia::Cook {

// ─── extension → asset type ──────────────────────────────────────────────────

std::string AssetTypeFromExtension(const fs::path& ext) {
    std::string e = ext.string();
    std::transform(e.begin(), e.end(), e.begin(), ::tolower);

    if (e == ".png"  || e == ".jpg" || e == ".jpeg" ||
        e == ".bmp"  || e == ".tga" || e == ".hdr")
        return "Texture";

    if (e == ".gltf" || e == ".glb")
        return "Mesh";

    return {};
}

// ─── default import settings per type ────────────────────────────────────────

static void ApplyDefaultSettings(MetaFile& meta, const fs::path& sourcePath) {
    if (meta.type == "Texture") {
        // Assume sRGB unless the file is under an hdri/ directory or is .hdr.
        const bool isHdr = sourcePath.extension().string() == ".hdr";
        const bool inHdri = [&] {
            for (auto& part : sourcePath) {
                if (part.string() == "hdri") return true;
            }
            return false;
        }();
        meta.settings["srgb"]    = (isHdr || inHdri) ? "0" : "1";
        meta.settings["mipmaps"] = "1";
    } else if (meta.type == "Mesh") {
        meta.settings["merge_submeshes"] = "0";
    }
}

// ─── ScanAndImport ───────────────────────────────────────────────────────────

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

        // Skip sidecar files and hidden files.
        const std::string ext = src.extension().string();
        if (ext == ".sameta" || src.filename().string().front() == '.') continue;

        const std::string type = AssetTypeFromExtension(src.extension());
        if (type.empty()) continue;

        const fs::path metaPath = MetaFile::MetaPathFor(src);

        AssetEntry ae;
        ae.sourcePath = src;
        ae.metaPath   = metaPath;

        if (fs::exists(metaPath)) {
            // Already imported — just load the existing meta.
            if (!MetaFile::Load(metaPath, ae.meta)) {
                std::cerr << "[Import] Failed to load existing meta: " << metaPath << '\n';
                continue;
            }
        } else {
            // New asset — generate UUID and default settings.
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

} // namespace StellarAlia::Cook
