#pragma once

#include <optional>
#include <string>

#include "resource/types/ImageData.hpp"

namespace StellarAlia::Resource {

// ─────────────────────────────────────────────────────────────────────────────
// ImageLoader  —  loads LDR and HDR images from disk via stb_image.
//
// LDR (PNG / JPG / BMP / TGA):  Load()    → 8-bit RGBA
// HDR (Radiance .hdr):           LoadHDR() → 32-bit float RGB
// ─────────────────────────────────────────────────────────────────────────────
class ImageLoader {
public:
    // Load an LDR image.  Always returns RGBA (4 channels) for GPU convenience.
    // Returns std::nullopt on error (error is logged via SA_LOG_ERROR).
    [[nodiscard]] static std::optional<ImageData> Load(const std::string& path);

    // Load an HDR Radiance image (.hdr).  Returns 3-channel float data.
    [[nodiscard]] static std::optional<ImageData> LoadHDR(const std::string& path);

    // Load from raw memory (e.g., embedded glTF buffer).
    [[nodiscard]] static std::optional<ImageData>
    LoadFromMemory(const uint8_t* data, size_t byteLen, const std::string& debugName = {});
};

} // namespace StellarAlia::Resource
