#include "TextureCook.hpp"

#include "resource/cook/CookedTexture.hpp"
#include "resource/loaders/ImageLoader.hpp"

#include <cstring>
#include <iostream>

namespace StellarAlia::Cook {

using namespace StellarAlia::Resource;

// ─── helpers ─────────────────────────────────────────────────────────────────

static bool NeedsRecook(const AssetEntry& entry, const fs::path& outPath) {
    if (!fs::exists(outPath)) return true;
    const auto outTime = fs::last_write_time(outPath);
    if (fs::last_write_time(entry.sourcePath) > outTime) return true;
    if (fs::exists(entry.metaPath) && fs::last_write_time(entry.metaPath) > outTime) return true;
    return false;
}

// ─── CookTexture ─────────────────────────────────────────────────────────────

bool CookTexture(const AssetEntry& entry, const fs::path& outputDir, bool force) {
    fs::create_directories(outputDir);

    const fs::path outPath = outputDir / (entry.meta.uuid.ToString() + ".satex");

    if (!force && !NeedsRecook(entry, outPath)) {
        std::cout << "[Cook] SKIP (up-to-date)  " << entry.sourcePath.filename() << '\n';
        return true;
    }

    const std::string srcStr = entry.sourcePath.string();
    const bool isHdr = entry.sourcePath.extension().string() == ".hdr";
    const bool srgb  = entry.meta.GetBool("srgb", true);

    // Load with the existing ImageLoader.
    std::optional<ImageData> imgOpt = isHdr
        ? ImageLoader::LoadHDR(srcStr)
        : ImageLoader::Load(srcStr);

    if (!imgOpt) {
        std::cerr << "[Cook] FAIL  " << entry.sourcePath.filename()
                  << " — could not load image\n";
        return false;
    }

    const ImageData& img = *imgOpt;

    // Build CookedTexture (single mip for now; mip generation in a later pass).
    CookedTexture cooked;
    cooked.id        = entry.meta.uuid;
    cooked.width     = img.width;
    cooked.height    = img.height;
    cooked.mipLevels = 1;
    cooked.srgb      = srgb;
    cooked.isHDR     = img.isHDR;
    cooked.format    = img.isHDR ? CookedTextureFormat::RGBA32F
                                 : CookedTextureFormat::RGBA8;

    // Populate data blob.
    CookedTextureMip mip0;
    mip0.offset = 0;
    if (img.isHDR) {
        mip0.size = img.pixelsHDR.size() * sizeof(float);
        cooked.data.resize(mip0.size);
        memcpy(cooked.data.data(), img.pixelsHDR.data(), mip0.size);
    } else {
        mip0.size = img.pixels.size();
        cooked.data = img.pixels;
    }
    cooked.mips.push_back(mip0);

    if (!SaveCookedTexture(cooked, outPath.string())) {
        std::cerr << "[Cook] FAIL  " << entry.sourcePath.filename()
                  << " — could not write .satex\n";
        return false;
    }

    std::cout << "[Cook] OK    " << entry.sourcePath.filename()
              << "  →  " << outPath.filename()
              << "  (" << img.width << 'x' << img.height
              << ' ' << (img.isHDR ? "RGBA32F" : "RGBA8") << ")\n";
    return true;
}

} // namespace StellarAlia::Cook
