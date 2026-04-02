// IblBake CLI
//
// Usage:
//   IblBake --hdr <path/to/panorama.hdr> --output <cook_cache_dir> [--force]
//
// Reads the .sameta sidecar next to the HDR to obtain its UUID, derives
// child UUIDs for irradiance/prefiltered/brdf_lut, then bakes all three.
// Prints the derived UUIDs so they can be pasted into .sascene IBL components.

#include "IblBake.hpp"
#include "cook/MetaFile.hpp"
#include "resource/loaders/ImageLoader.hpp"
#include "core/logs/Log.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using namespace StellarAlia;

int main(int argc, char** argv) {
    Core::Log::Initialize();

    fs::path hdrPath, outputDir;
    bool force = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--hdr"    && i + 1 < argc) { hdrPath   = argv[++i]; }
        else if (arg == "--output" && i + 1 < argc) { outputDir = argv[++i]; }
        else if (arg == "--force")              { force = true; }
    }

    if (hdrPath.empty() || outputDir.empty()) {
        std::cerr << "Usage: IblBake --hdr <panorama.hdr> --output <cook_cache> [--force]\n";
        return 1;
    }

    // Load .sameta to get the HDR's UUID.
    fs::path metaPath = Cook::MetaFile::MetaPathFor(hdrPath);
    Cook::MetaFile meta;
    if (!Cook::MetaFile::Load(metaPath, meta)) {
        SA_LOG_ERROR("IblBake: cannot load meta '{}'", metaPath.string());
        SA_LOG_ERROR("         Run CookAssets first to generate .sameta files.");
        return 1;
    }
    SA_LOG_INFO("IblBake: HDR UUID = {}", meta.uuid.ToString());

    IblBake::IblAssetIDs ids = IblBake::DeriveIDs(meta.uuid);
    SA_LOG_INFO("IblBake: irradiance  UUID = {}", ids.irradiance.ToString());
    SA_LOG_INFO("IblBake: prefiltered UUID = {}", ids.prefiltered.ToString());
    SA_LOG_INFO("IblBake: brdf_lut    UUID = {}", ids.brdfLut.ToString());

    // Load the HDR image.
    auto hdrOpt = Resource::ImageLoader::LoadHDR(hdrPath.string());
    if (!hdrOpt) {
        SA_LOG_ERROR("IblBake: failed to load HDR '{}'", hdrPath.string());
        return 1;
    }
    SA_LOG_INFO("IblBake: loaded HDR {}×{}", hdrOpt->width, hdrOpt->height);

    bool ok = IblBake::Bake(*hdrOpt, ids, outputDir, force);

    Core::Log::Shutdown();
    return ok ? 0 : 1;
}
