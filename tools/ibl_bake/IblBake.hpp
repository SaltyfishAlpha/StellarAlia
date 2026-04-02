#pragma once

// CPU-side IBL precomputation.
//
// Produces three cooked assets from an HDR equirectangular panorama:
//   irradiance  — 256×128,  RGBA32F, 1 mip  (diffuse irradiance)
//   prefiltered — 512×256,  RGBA32F, 5 mips  (specular, mip = roughness level)
//   brdf_lut    — 512×512,  RGBA32F, 1 mip  (split-sum scale/bias in RG)
//
// Child UUID derivation (deterministic from the HDR's UUID):
//   irradiance  hi ^= 0x0100000000000001, lo ^= 0x0100000000000001
//   prefiltered hi ^= 0x0200000000000002, lo ^= 0x0200000000000002
//   brdf_lut    = fixed "c5b06992-5a8f-4dc9-9d11-406e12b969d4"

#include "core/asset/AssetID.hpp"
#include "resource/types/ImageData.hpp"
#include "resource/cook/CookedTexture.hpp"

#include <filesystem>

namespace StellarAlia::IblBake {

// Derive the three child AssetIDs from a parent HDR UUID.
struct IblAssetIDs {
    AssetID irradiance;
    AssetID prefiltered;
    AssetID brdfLut;
};
IblAssetIDs DeriveIDs(const AssetID& hdrID);

// Bake all three IBL maps and write them as .satex files into outputDir.
// Returns true if all three files were written successfully.
// Skips a file if it already exists and force=false.
bool Bake(const Resource::ImageData& hdr,
          const IblAssetIDs&         ids,
          const std::filesystem::path& outputDir,
          bool force = false);

} // namespace StellarAlia::IblBake
