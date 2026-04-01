#include "CookedTexture.hpp"

#include <fstream>
#include <cstring>

namespace StellarAlia::Resource {

bool SaveCookedTexture(const CookedTexture& tex, const std::string& path) {
    if (!tex.IsValid()) return false;

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;

    SatexFormat::FileHeader hdr{};
    hdr.magic     = SatexFormat::Magic;
    hdr.version   = SatexFormat::Version;
    hdr.uuid_hi   = tex.id.hi;
    hdr.uuid_lo   = tex.id.lo;
    hdr.width     = tex.width;
    hdr.height    = tex.height;
    hdr.mip_count = tex.mipLevels;
    hdr.format    = static_cast<uint32_t>(tex.format);
    if (tex.srgb)  hdr.flags |= SatexFormat::Flag_SRGB;
    if (tex.isHDR) hdr.flags |= SatexFormat::Flag_HDR;

    f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

    for (const auto& mip : tex.mips) {
        SatexFormat::MipEntry entry{ mip.offset, mip.size };
        f.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
    }

    f.write(reinterpret_cast<const char*>(tex.data.data()),
            static_cast<std::streamsize>(tex.data.size()));

    return f.good();
}

bool LoadCookedTexture(const std::string& path, CookedTexture& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    SatexFormat::FileHeader hdr{};
    f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!f || hdr.magic != SatexFormat::Magic || hdr.version != SatexFormat::Version)
        return false;

    out.id.hi     = hdr.uuid_hi;
    out.id.lo     = hdr.uuid_lo;
    out.width     = hdr.width;
    out.height    = hdr.height;
    out.mipLevels = hdr.mip_count;
    out.format    = static_cast<CookedTextureFormat>(hdr.format);
    out.srgb      = (hdr.flags & SatexFormat::Flag_SRGB) != 0;
    out.isHDR     = (hdr.flags & SatexFormat::Flag_HDR)  != 0;

    out.mips.resize(hdr.mip_count);
    for (auto& mip : out.mips) {
        SatexFormat::MipEntry entry{};
        f.read(reinterpret_cast<char*>(&entry), sizeof(entry));
        if (!f) return false;
        mip.offset = entry.offset;
        mip.size   = entry.size;
    }

    // Read the rest as pixel data.
    const auto dataStart = f.tellg();
    f.seekg(0, std::ios::end);
    const auto dataSize = static_cast<size_t>(f.tellg() - dataStart);
    f.seekg(dataStart);

    out.data.resize(dataSize);
    f.read(reinterpret_cast<char*>(out.data.data()), static_cast<std::streamsize>(dataSize));

    return f.good() || f.eof();
}

} // namespace StellarAlia::Resource
