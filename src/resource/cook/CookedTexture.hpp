#pragma once

#include "core/asset/AssetID.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace StellarAlia::Resource {

// GPU pixel format stored in the cooked file.
// Compression formats (BC7, ASTC) are placeholders for future cook passes.
enum class CookedTextureFormat : uint32_t {
    RGBA8       = 0,   // 8-bit per channel, LDR
    RGBA32F     = 1,   // 32-bit float per channel, HDR
    BC7         = 2,   // Block compressed LDR (future)
    ASTC_6x6    = 3,   // Mobile ASTC (future)
};

// Per-mip descriptor inside the data blob.
struct CookedTextureMip {
    uint64_t offset = 0;  // byte offset from start of CookedTexture::data
    uint64_t size   = 0;  // byte size of this mip level
};

// In-memory representation loaded from a .satex file.
struct CookedTexture {
    AssetID              id;
    uint32_t             width     = 0;
    uint32_t             height    = 0;
    uint32_t             mipLevels = 1;
    CookedTextureFormat  format    = CookedTextureFormat::RGBA8;
    bool                 srgb      = false;
    bool                 isHDR     = false;
    // When true the texture is a cubemap (6 faces).
    // Each mip entry's data block contains all 6 faces: face0|face1|...|face5.
    // The GPU image is created with VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT.
    bool                 cubemap   = false;

    std::vector<CookedTextureMip> mips; // mips.size() == mipLevels
    std::vector<uint8_t>          data; // all mip levels concatenated

    bool IsValid() const { return width > 0 && height > 0 && !data.empty(); }

    const uint8_t* MipData(uint32_t mip) const {
        if (mip >= mips.size()) return nullptr;
        return data.data() + mips[mip].offset;
    }
    size_t MipSize(uint32_t mip) const {
        if (mip >= mips.size()) return 0;
        return static_cast<size_t>(mips[mip].size);
    }
};

// ─── .satex binary layout ────────────────────────────────────────────────────
//
//  FileHeader           (48 bytes)
//  MipEntry[mip_count]  (16 bytes each)
//  raw pixel data       (all mips concatenated)
//
namespace SatexFormat {
    static constexpr uint32_t Magic   = 0x58455453u; // 'STEX' LE
    static constexpr uint32_t Version = 1u;

#pragma pack(push, 1)
    struct FileHeader {
        uint32_t magic;
        uint32_t version;
        uint64_t uuid_hi;
        uint64_t uuid_lo;
        uint32_t width;
        uint32_t height;
        uint32_t mip_count;
        uint32_t format;   // CookedTextureFormat
        uint32_t flags;    // bit 0 = srgb, bit 1 = hdr
        uint32_t _pad;
    };
    static_assert(sizeof(FileHeader) == 48);

    struct MipEntry {
        uint64_t offset;
        uint64_t size;
    };
    static_assert(sizeof(MipEntry) == 16);
#pragma pack(pop)

    static constexpr uint32_t Flag_SRGB   = 1u << 0;
    static constexpr uint32_t Flag_HDR    = 1u << 1;
    static constexpr uint32_t Flag_Cubemap = 1u << 2;
} // namespace SatexFormat

bool SaveCookedTexture(const CookedTexture& tex, const std::string& path);
bool LoadCookedTexture(const std::string& path, CookedTexture& out);

} // namespace StellarAlia::Resource
