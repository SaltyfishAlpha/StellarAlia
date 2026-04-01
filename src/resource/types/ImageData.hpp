#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace StellarAlia::Resource {

// ─────────────────────────────────────────────────────────────────────────────
// ImageData  —  CPU-side image loaded from disk.
//
// LDR images  (PNG / JPG / BMP / TGA): pixels filled, pixelsHDR empty.
// HDR images  (.hdr Radiance RGBE):    pixelsHDR filled, pixels empty.
//
// channels: 1=R  2=RG  3=RGB  4=RGBA
// ─────────────────────────────────────────────────────────────────────────────
struct ImageData {
    std::string           path;
    uint32_t              width    = 0;
    uint32_t              height   = 0;
    uint32_t              channels = 0;
    bool                  isHDR   = false;

    std::vector<uint8_t>  pixels;      // LDR: width * height * channels bytes
    std::vector<float>    pixelsHDR;   // HDR: width * height * channels floats

    [[nodiscard]] bool IsValid()  const noexcept { return width > 0 && height > 0; }
    [[nodiscard]] size_t ByteSize() const noexcept {
        return isHDR ? pixelsHDR.size() * sizeof(float) : pixels.size();
    }
};

} // namespace StellarAlia::Resource
