#include "CookedSH9.hpp"

#include <fstream>
#include <cstring>

namespace StellarAlia::Resource {

bool SaveCookedSH9(const CookedSH9& sh9, const std::string& path) {
    if (!sh9.id.IsValid()) return false;

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;

    Sash9Format::FileHeader hdr{};
    hdr.magic   = Sash9Format::Magic;
    hdr.version = Sash9Format::Version;
    hdr.uuid_hi = sh9.id.hi;
    hdr.uuid_lo = sh9.id.lo;
    f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

    // Write 9 × float[4] = 144 bytes
    for (const auto& c : sh9.coeffs) {
        float v[4] = { c.x, c.y, c.z, c.w };
        f.write(reinterpret_cast<const char*>(v), sizeof(v));
    }

    return f.good();
}

bool LoadCookedSH9(const std::string& path, CookedSH9& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    Sash9Format::FileHeader hdr{};
    f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!f || hdr.magic != Sash9Format::Magic || hdr.version != Sash9Format::Version)
        return false;

    out.id.hi = hdr.uuid_hi;
    out.id.lo = hdr.uuid_lo;

    for (auto& c : out.coeffs) {
        float v[4]{};
        f.read(reinterpret_cast<char*>(v), sizeof(v));
        if (!f) return false;
        c = { v[0], v[1], v[2], v[3] };
    }

    return true;
}

} // namespace StellarAlia::Resource
