#pragma once

#include "core/asset/AssetID.hpp"
#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <string>

namespace StellarAlia::Resource {

// In-memory representation of a .sash9 file.
// Stores the 9 L0+L1+L2 spherical harmonic coefficients (RGB, vec4-padded)
// produced by ProjectHDRtoSH and pre-multiplied by the Lambertian convolution
// kernel.  The w component of each vec4 is always 0 (padding for std140).
struct CookedSH9 {
    AssetID                  id;
    std::array<glm::vec4, 9> coeffs = {};

    bool IsValid() const { return id.IsValid(); }
};

// ─── .sash9 binary layout (168 bytes total) ───────────────────────────────────
//
//  FileHeader  (24 bytes)
//  float[9][4] (144 bytes) — 9 vec4 SH coefficients, row-major
//
namespace Sash9Format {
    static constexpr uint32_t Magic   = 0x39485353u;  // 'SSH9' little-endian
    static constexpr uint32_t Version = 1u;

#pragma pack(push, 1)
    struct FileHeader {
        uint32_t magic;
        uint32_t version;
        uint64_t uuid_hi;
        uint64_t uuid_lo;
    };
    static_assert(sizeof(FileHeader) == 24);
#pragma pack(pop)
} // namespace Sash9Format

bool SaveCookedSH9(const CookedSH9& sh9, const std::string& path);
bool LoadCookedSH9(const std::string& path, CookedSH9& out);

} // namespace StellarAlia::Resource
