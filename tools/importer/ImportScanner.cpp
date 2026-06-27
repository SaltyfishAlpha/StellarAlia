#include "importer/ImportScanner.hpp"

#include "importer/MeshImporter.hpp"
#include "importer/TextureImporter.hpp"
#include "importer/MaterialImporter.hpp"
#include "importer/InputMapImporter.hpp"

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

    if (e == ".samat")
        return "Material";

    if (e == ".saglsl")
        return "Shader";

    if (e == ".sascene")
        return "Scene";

    if (e == ".cs")
        return "Script";

    if (e == ".sainputmap")
        return "InputMap";

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
    } else if (meta.type == "Script") {
        // C# class name defaults to file stem; users can edit the .sameta to
        // override (e.g. when the class is in a sub-namespace or differs from
        // the filename). Scan never overwrites an existing sameta.
        meta.settings["class_name"] = sourcePath.stem().string();
    }
}

AssetEntry EnsureMeta(const fs::path& srcPath, const std::string& type) {
    const fs::path metaPath = MetaFile::MetaPathFor(srcPath);

    AssetEntry ae;
    ae.sourcePath = srcPath;
    ae.metaPath   = metaPath;

    if (fs::exists(metaPath)) {
        if (!MetaFile::Load(metaPath, ae.meta)) {
            std::cerr << "[Import] Failed to load existing meta: " << metaPath << '\n';
        }
        return ae;
    }

    ae.meta.uuid = AssetID::Generate();
    ae.meta.type = type;
    ApplyDefaultSettings(ae.meta, srcPath);

    if (!MetaFile::Save(metaPath, ae.meta)) {
        std::cerr << "[Import] Failed to write meta: " << metaPath << '\n';
    } else {
        std::cout << "[Import] " << srcPath.filename() << "  →  "
                  << ae.meta.uuid.ToString() << '\n';
    }
    return ae;
}

void CookAssetEntry(const AssetEntry& entry, const fs::path& cookCacheDir) {
    if (cookCacheDir.empty()) return;
    const std::string& t = entry.meta.type;
    if      (t == "Mesh")     CookMesh(entry, cookCacheDir, /*force=*/false);
    else if (t == "Texture")  CookTexture(entry, cookCacheDir, /*force=*/false);
    else if (t == "Material") CookStandaloneMaterial(entry.sourcePath, entry.meta.uuid,
                                                     cookCacheDir, /*force=*/false);
    else if (t == "InputMap") CookInputMap(entry, cookCacheDir, /*force=*/false);
    // Script, Scene, Shader: no cooked output.
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

        // Skip sidecars, cooked outputs, and hidden files.
        if (ext == ".sameta" || ext == ".sanim" || ext == ".saskel" || ext == ".samatc" ||
            src.filename().string().front() == '.')
            continue;

        const std::string type = AssetTypeFromExtension(src.extension());
        if (type.empty()) continue;

        AssetEntry ae = EnsureMeta(src, type);
        if (!ae.meta.IsValid()) continue;

        results.push_back(std::move(ae));
    }

    return results;
}

} // namespace StellarAlia::Import
