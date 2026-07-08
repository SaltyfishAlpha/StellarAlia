#include "resource/loaders/DdsLoader.hpp"

#include "core/io/FileIO.hpp"
#include "core/logs/Log.hpp"

#if __has_include(<bcdec.h>)
#define BCDEC_IMPLEMENTATION
#include <bcdec.h>
#define SA_HAS_BCDEC 1
#endif

#include <algorithm>
#include <cstring>

namespace StellarAlia::Resource {

namespace {

// Microsoft DDS layout (docs: "DDS File Reference"). All fields little-endian.
#pragma pack(push, 1)
struct DdsPixelFormat {
    uint32_t size;          // 32
    uint32_t flags;
    uint32_t fourCC;
    uint32_t rgbBitCount;
    uint32_t rMask, gMask, bMask, aMask;
};
struct DdsHeader {
    uint32_t size;          // 124
    uint32_t flags;
    uint32_t height;
    uint32_t width;
    uint32_t pitchOrLinearSize;
    uint32_t depth;
    uint32_t mipMapCount;
    uint32_t reserved1[11];
    DdsPixelFormat pf;
    uint32_t caps;
    uint32_t caps2;
    uint32_t caps3, caps4;
    uint32_t reserved2;
};
struct DdsHeaderDx10 {
    uint32_t dxgiFormat;
    uint32_t resourceDimension;
    uint32_t miscFlag;      // bit 2 (0x4) = TEXTURECUBE
    uint32_t arraySize;
    uint32_t miscFlags2;
};
#pragma pack(pop)
static_assert(sizeof(DdsHeader) == 124);
static_assert(sizeof(DdsHeaderDx10) == 20);

constexpr uint32_t DdsMagic       = 0x20534444u; // "DDS "
constexpr uint32_t DDPF_FOURCC    = 0x4;
constexpr uint32_t DDPF_RGB       = 0x40;
constexpr uint32_t DDSCAPS2_CUBEMAP           = 0x200;
constexpr uint32_t DDSCAPS2_CUBEMAP_ALLFACES  = 0xFC00;
constexpr uint32_t DDS_DIMENSION_TEXTURE2D    = 3;
constexpr uint32_t DDS_MISC_TEXTURECUBE       = 0x4;

constexpr uint32_t FourCC(char a, char b, char c, char d) {
    return uint32_t(uint8_t(a)) | uint32_t(uint8_t(b)) << 8 |
           uint32_t(uint8_t(c)) << 16 | uint32_t(uint8_t(d)) << 24;
}

// DXGI_FORMAT values we accept (from dxgiformat.h).
enum DxgiFormat : uint32_t {
    DXGI_R8G8B8A8_UNORM      = 28,
    DXGI_R8G8B8A8_UNORM_SRGB = 29,
    DXGI_BC1_UNORM           = 71,
    DXGI_BC1_UNORM_SRGB      = 72,
    DXGI_BC3_UNORM           = 77,
    DXGI_BC3_UNORM_SRGB      = 78,
    DXGI_BC5_UNORM           = 83,
    DXGI_BC7_UNORM           = 98,
    DXGI_BC7_UNORM_SRGB      = 99,
};

uint64_t MipByteSize(CookedTextureFormat fmt, uint32_t w, uint32_t h) {
    if (IsBlockCompressed(fmt)) {
        const uint64_t blockBytes = fmt == CookedTextureFormat::BC1 ? 8u : 16u;
        return uint64_t(std::max(1u, (w + 3) / 4)) * std::max(1u, (h + 3) / 4) * blockBytes;
    }
    const uint64_t bpp = fmt == CookedTextureFormat::RGBA32F ? 16u : 4u;
    return uint64_t(w) * h * bpp;
}

} // namespace

std::optional<DdsLoader::Result> DdsLoader::Load(const std::string& path) {
    auto bytesOpt = IO::ReadBytes(path);
    if (!bytesOpt) {
        SA_LOG_ERROR("DdsLoader: cannot read '{}'", path);
        return std::nullopt;
    }
    std::vector<uint8_t>& bytes = *bytesOpt;
    if (bytes.size() < 4 + sizeof(DdsHeader)) {
        SA_LOG_ERROR("DdsLoader: '{}' too small for a DDS header", path);
        return std::nullopt;
    }

    uint32_t magic = 0;
    std::memcpy(&magic, bytes.data(), 4);
    DdsHeader hdr{};
    std::memcpy(&hdr, bytes.data() + 4, sizeof(hdr));
    if (magic != DdsMagic || hdr.size != 124 || hdr.pf.size != 32) {
        SA_LOG_ERROR("DdsLoader: '{}' is not a valid DDS file", path);
        return std::nullopt;
    }

    size_t dataOffset = 4 + sizeof(DdsHeader);

    Result res;
    CookedTexture& tex = res.tex;
    tex.width  = hdr.width;
    tex.height = hdr.height;

    bool swapBgra = false;  // legacy 32-bit BGRA masks → swizzle to RGBA

    if ((hdr.pf.flags & DDPF_FOURCC) && hdr.pf.fourCC == FourCC('D','X','1','0')) {
        if (bytes.size() < dataOffset + sizeof(DdsHeaderDx10)) {
            SA_LOG_ERROR("DdsLoader: '{}' truncated DX10 header", path);
            return std::nullopt;
        }
        DdsHeaderDx10 dx10{};
        std::memcpy(&dx10, bytes.data() + dataOffset, sizeof(dx10));
        dataOffset += sizeof(dx10);

        if (dx10.resourceDimension != DDS_DIMENSION_TEXTURE2D || dx10.arraySize > 1) {
            SA_LOG_ERROR("DdsLoader: '{}' — only 2D non-array textures supported", path);
            return std::nullopt;
        }
        if (dx10.miscFlag & DDS_MISC_TEXTURECUBE) tex.cubemap = true;

        switch (dx10.dxgiFormat) {
            case DXGI_BC1_UNORM:           tex.format = CookedTextureFormat::BC1; break;
            case DXGI_BC1_UNORM_SRGB:      tex.format = CookedTextureFormat::BC1; res.forceSrgb = true; break;
            case DXGI_BC3_UNORM:           tex.format = CookedTextureFormat::BC3; break;
            case DXGI_BC3_UNORM_SRGB:      tex.format = CookedTextureFormat::BC3; res.forceSrgb = true; break;
            case DXGI_BC5_UNORM:           tex.format = CookedTextureFormat::BC5; break;
            case DXGI_BC7_UNORM:           tex.format = CookedTextureFormat::BC7; break;
            case DXGI_BC7_UNORM_SRGB:      tex.format = CookedTextureFormat::BC7; res.forceSrgb = true; break;
            case DXGI_R8G8B8A8_UNORM:      tex.format = CookedTextureFormat::RGBA8; break;
            case DXGI_R8G8B8A8_UNORM_SRGB: tex.format = CookedTextureFormat::RGBA8; res.forceSrgb = true; break;
            default:
                SA_LOG_ERROR("DdsLoader: '{}' — unsupported DXGI format {} "
                             "(BC2/BC6H etc. not handled)", path, dx10.dxgiFormat);
                return std::nullopt;
        }
    } else if (hdr.pf.flags & DDPF_FOURCC) {
        switch (hdr.pf.fourCC) {
            case FourCC('D','X','T','1'): tex.format = CookedTextureFormat::BC1; break;
            case FourCC('D','X','T','4'):
            case FourCC('D','X','T','5'): tex.format = CookedTextureFormat::BC3; break;
            case FourCC('A','T','I','2'):
            case FourCC('B','C','5','U'): tex.format = CookedTextureFormat::BC5; break;
            default: {
                char cc[5] = {char(hdr.pf.fourCC), char(hdr.pf.fourCC >> 8),
                              char(hdr.pf.fourCC >> 16), char(hdr.pf.fourCC >> 24), 0};
                SA_LOG_ERROR("DdsLoader: '{}' — unsupported FourCC '{}' "
                             "(DXT3/BC2 is rejected by design)", path, cc);
                return std::nullopt;
            }
        }
        if (hdr.caps2 & DDSCAPS2_CUBEMAP) {
            if ((hdr.caps2 & DDSCAPS2_CUBEMAP_ALLFACES) != DDSCAPS2_CUBEMAP_ALLFACES) {
                SA_LOG_ERROR("DdsLoader: '{}' — partial cubemaps not supported", path);
                return std::nullopt;
            }
            tex.cubemap = true;
        }
    } else if ((hdr.pf.flags & DDPF_RGB) && hdr.pf.rgbBitCount == 32) {
        tex.format = CookedTextureFormat::RGBA8;
        if (hdr.pf.rMask == 0x00FF0000u) swapBgra = true;        // BGRA
        else if (hdr.pf.rMask != 0x000000FFu) {
            SA_LOG_ERROR("DdsLoader: '{}' — unsupported channel masks", path);
            return std::nullopt;
        }
        if (hdr.caps2 & DDSCAPS2_CUBEMAP) tex.cubemap = true;
    } else {
        SA_LOG_ERROR("DdsLoader: '{}' — unsupported pixel format (bitCount={})",
                     path, hdr.pf.rgbBitCount);
        return std::nullopt;
    }

    tex.mipLevels = std::max(1u, hdr.mipMapCount);
    const uint32_t faces = tex.cubemap ? 6u : 1u;

    // DDS stores face-major: all mips of face 0, then face 1… CookedTexture
    // wants mip-major (each mip = face0|…|face5), so gather per-face spans.
    std::vector<uint64_t> mipSize(tex.mipLevels);
    uint64_t faceBytes = 0;
    {
        uint32_t w = tex.width, h = tex.height;
        for (uint32_t m = 0; m < tex.mipLevels; ++m) {
            mipSize[m] = MipByteSize(tex.format, w, h);
            faceBytes += mipSize[m];
            w = std::max(1u, w / 2); h = std::max(1u, h / 2);
        }
    }
    const uint64_t needed = faceBytes * faces;
    if (bytes.size() < dataOffset + needed) {
        SA_LOG_ERROR("DdsLoader: '{}' — payload truncated ({} < {})",
                     path, bytes.size() - dataOffset, needed);
        return std::nullopt;
    }

    tex.data.resize(needed);
    tex.mips.resize(tex.mipLevels);
    uint64_t outOffset = 0;
    // Per-face offset of each mip inside the source face block.
    std::vector<uint64_t> mipOffsetInFace(tex.mipLevels);
    for (uint32_t m = 1; m < tex.mipLevels; ++m)
        mipOffsetInFace[m] = mipOffsetInFace[m - 1] + mipSize[m - 1];

    for (uint32_t m = 0; m < tex.mipLevels; ++m) {
        tex.mips[m].offset = outOffset;
        tex.mips[m].size   = mipSize[m] * faces;
        for (uint32_t f = 0; f < faces; ++f) {
            const uint8_t* src = bytes.data() + dataOffset +
                                 f * faceBytes + mipOffsetInFace[m];
            std::memcpy(tex.data.data() + outOffset, src, mipSize[m]);
            outOffset += mipSize[m];
        }
    }

    if (swapBgra)
        for (size_t i = 0; i + 3 < tex.data.size(); i += 4)
            std::swap(tex.data[i], tex.data[i + 2]);

    return res;
}

// ── DecodeToRGBA8 ────────────────────────────────────────────────────────────

std::optional<ImageData> DdsLoader::DecodeToRGBA8(const CookedTexture& tex, uint32_t mip) {
    if (mip >= tex.mipLevels || !tex.MipData(mip)) return std::nullopt;

    const uint32_t w = std::max(1u, tex.width >> mip);
    const uint32_t h = std::max(1u, tex.height >> mip);

    ImageData img;
    img.width    = w;
    img.height   = h;
    img.channels = 4;
    img.pixels.resize(size_t(w) * h * 4);

    const uint8_t* src = tex.MipData(mip);  // cubemap: face 0 is first

    if (tex.format == CookedTextureFormat::RGBA8) {
        std::memcpy(img.pixels.data(), src, img.pixels.size());
        return img;
    }
    if (!IsBlockCompressed(tex.format)) return std::nullopt;

#ifdef SA_HAS_BCDEC
    const uint32_t bw = std::max(1u, (w + 3) / 4);
    const uint32_t bh = std::max(1u, (h + 3) / 4);
    const uint32_t blockBytes = tex.format == CookedTextureFormat::BC1 ? 8u : 16u;

    // Edge blocks of non-multiple-of-4 mips decode into a padded row buffer,
    // then the valid texels are clipped out.
    std::vector<uint8_t> padded;
    for (uint32_t by = 0; by < bh; ++by) {
        for (uint32_t bx = 0; bx < bw; ++bx) {
            const uint8_t* block = src + (size_t(by) * bw + bx) * blockBytes;
            const uint32_t px = bx * 4, py = by * 4;
            const bool clipped = px + 4 > w || py + 4 > h;

            uint8_t* dst;
            int      pitch;
            if (clipped) {
                padded.assign(4 * 4 * 4, 0);
                dst   = padded.data();
                pitch = 4 * 4;
            } else {
                dst   = img.pixels.data() + (size_t(py) * w + px) * 4;
                pitch = static_cast<int>(w * 4);
            }

            switch (tex.format) {
                case CookedTextureFormat::BC1: bcdec_bc1(block, dst, pitch); break;
                case CookedTextureFormat::BC3: bcdec_bc3(block, dst, pitch); break;
                case CookedTextureFormat::BC7: bcdec_bc7(block, dst, pitch); break;
                case CookedTextureFormat::BC5: {
                    // bcdec emits 2 bytes/px RG — expand in place to RGBA.
                    uint8_t rg[4 * 4 * 2];
                    bcdec_bc5(block, rg, 4 * 2);
                    for (int i = 0; i < 16; ++i) {
                        uint8_t* p = clipped ? padded.data() + i * 4
                                             : dst + (i / 4) * pitch + (i % 4) * 4;
                        p[0] = rg[i * 2]; p[1] = rg[i * 2 + 1]; p[2] = 0; p[3] = 255;
                    }
                    if (!clipped) continue;  // already written via dst/pitch
                    break;
                }
                default: return std::nullopt;
            }

            if (clipped) {
                for (uint32_t y = py; y < std::min(py + 4, h); ++y)
                    std::memcpy(img.pixels.data() + (size_t(y) * w + px) * 4,
                                padded.data() + (y - py) * 4 * 4,
                                size_t(std::min(4u, w - px)) * 4);
            }
        }
    }
    return img;
#else
    return std::nullopt;
#endif
}

} // namespace StellarAlia::Resource
