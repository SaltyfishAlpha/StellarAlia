#pragma once

#include "core/asset/AssetID.hpp"
#include "resource/types/AnimData.hpp"
#include <string>
#include <vector>

namespace StellarAlia::Resource {

// ─── In-memory representation of a cooked skeleton (.saskel) ─────────────────

struct CookedSkeleton {
    AssetID               id;
    std::vector<BoneInfo> bones;   // bone[i].parentIndex < i (parents before children)

    bool IsValid() const { return !bones.empty(); }
};

// ─── .saskel binary layout v1 ─────────────────────────────────────────────────
//
//  FileHeader  (32 bytes)
//  BoneEntry[bone_count]  (136 bytes each)
//
namespace SaskelFormat {
    static constexpr uint32_t Magic   = 0x4C454B53u;  // 'SKEL' LE
    static constexpr uint32_t Version = 1u;

#pragma pack(push, 1)
    struct FileHeader {
        uint32_t magic;
        uint32_t version;
        uint64_t uuid_hi;
        uint64_t uuid_lo;
        uint32_t bone_count;
        uint32_t _pad;
    };
    static_assert(sizeof(FileHeader) == 32);

    struct BoneEntry {
        char     name[64];       // null-terminated, truncated to 63 chars
        int32_t  parent_index;   // -1 = root
        uint32_t _pad;
        float    inv_bind[16];   // column-major glm::mat4
    };
    static_assert(sizeof(BoneEntry) == 136);
#pragma pack(pop)
} // namespace SaskelFormat

bool SaveCookedSkeleton(const CookedSkeleton& skel, const std::string& path);
bool LoadCookedSkeleton(const std::string& path, CookedSkeleton& out);

// Derive a deterministic skeleton AssetID from a mesh UUID and skin index.
// Call with the same meshId and skinIndex as used during cooking.
inline AssetID DeriveSkinID(const AssetID& meshId, int32_t skinIndex) {
    const uint64_t idx = static_cast<uint64_t>(skinIndex) + 1u;
    AssetID id;
    id.hi = meshId.hi ^ (idx * 0x517cc1b727220a95ULL);
    id.lo = meshId.lo ^ (idx * 0x9e3779b97f4a7c15ULL);
    id.hi = (id.hi & 0xFFFFFFFFFFFF0FFFull) | 0x0000000000004000ull;
    id.lo = (id.lo & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;
    return id;
}

} // namespace StellarAlia::Resource
