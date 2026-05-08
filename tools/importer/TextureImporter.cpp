#include "importer/TextureImporter.hpp"

#include "resource/cook/CookedTexture.hpp"
#include "resource/loaders/ImageLoader.hpp"

#include <cstring>
#include <iostream>

namespace StellarAlia::Import {

using namespace StellarAlia::Resource;

static bool NeedsRecook(const AssetEntry& entry, const fs::path& outPath) {
    if (!fs::exists(outPath)) return true;
    const auto outTime = fs::last_write_time(outPath);
    if (fs::last_write_time(entry.sourcePath) > outTime) return true;
    if (fs::exists(entry.metaPath) && fs::last_write_time(entry.metaPath) > outTime) return true;
    return false;
}

bool CookTexture(const AssetEntry& entry, const fs::path& cookCacheDir, bool force) {
    fs::create_directories(cookCacheDir);

    const fs::path outPath = cookCacheDir / (entry.meta.uuid.ToString() + ".satex");

    if (!force && !NeedsRecook(entry, outPath)) {
        std::cout << "[Cook] SKIP (up-to-date)  " << entry.sourcePath.filename() << '\n';
        return true;
    }

    const bool isSrgb = entry.meta.GetBool("srgb", true);
    const bool isHdr  = entry.sourcePath.extension().string() == ".hdr";

    const std::optional<ImageData> imgOpt = isHdr
        ? ImageLoader::LoadHDR(entry.sourcePath.string())
        : ImageLoader::Load(entry.sourcePath.string());
    if (!imgOpt) {
        std::cerr << "[Cook] FAIL  " << entry.sourcePath.filename()
                  << " — could not load image\n";
        return false;
    }
    const ImageData& img = *imgOpt;

    CookedTexture cooked;
    cooked.id        = entry.meta.uuid;
    cooked.width     = img.width;
    cooked.height    = img.height;
    cooked.mipLevels = 1;
    cooked.srgb      = isSrgb && !isHdr;
    cooked.isHDR     = isHdr;
    cooked.format    = isHdr ? CookedTextureFormat::RGBA32F : CookedTextureFormat::RGBA8;

    CookedTextureMip mip0;
    mip0.offset = 0;
    if (isHdr) {
        mip0.size = img.pixelsHDR.size() * sizeof(float);
        cooked.data.resize(mip0.size);
        std::memcpy(cooked.data.data(), img.pixelsHDR.data(), mip0.size);
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

    std::cout << "[Cook] TEX   " << entry.sourcePath.filename()
              << "  →  " << outPath.filename()
              << "  (" << img.width << 'x' << img.height
              << (isHdr ? " HDR" : isSrgb ? " sRGB" : " linear")
              << ")\n";
    return true;
}

} // namespace StellarAlia::Import
