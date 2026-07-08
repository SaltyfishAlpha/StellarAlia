#pragma once

#include <optional>
#include <string>

#include "resource/cook/CookedTexture.hpp"
#include "resource/types/ImageData.hpp"

namespace StellarAlia::Resource {

// ─────────────────────────────────────────────────────────────────────────────
// DdsLoader — DDS container → CookedTexture pass-through (Issue #108).
//
// BCn block payloads and the authored mip chain are repackaged verbatim into
// the CookedTexture layout — no decode, no re-encode. Supports:
//   - legacy FourCC headers: DXT1 → BC1, DXT4/DXT5 → BC3
//   - DX10 extended headers: BC1/BC3/BC5/BC7 (+ their *_SRGB variants),
//     R8G8B8A8_UNORM(_SRGB) uncompressed
//   - cubemaps (6 faces; face-major per mip, matching Flag_Cubemap layout)
// Rejected (log + nullopt): BC2/DXT3, BC6H, volume textures, array textures,
// legacy uncompressed masks other than 32-bit RGBA/BGRA.
//
// srgb resolution: DX10 *_SRGB formats force srgb=true; otherwise the caller
// decides (`.sameta` srgb setting) — legacy headers carry no color-space info.
// The returned CookedTexture has id unset; the cook side fills it.
// ─────────────────────────────────────────────────────────────────────────────
class DdsLoader {
public:
    struct Result {
        CookedTexture tex;
        bool          forceSrgb = false;  // DX10 said sRGB — overrides .sameta
    };
    [[nodiscard]] static std::optional<Result> Load(const std::string& path);

    // CPU-decode one mip (face 0 for cubemaps) to 8-bit RGBA via bcdec —
    // editor thumbnails only; the renderer consumes the blocks directly.
    // Returns nullopt for unsupported formats or when bcdec is unavailable.
    [[nodiscard]] static std::optional<ImageData>
    DecodeToRGBA8(const CookedTexture& tex, uint32_t mip = 0);
};

} // namespace StellarAlia::Resource
