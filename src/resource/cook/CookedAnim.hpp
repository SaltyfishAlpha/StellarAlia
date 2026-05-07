#pragma once

#include "core/asset/AssetID.hpp"
#include "resource/types/AnimData.hpp"
#include <string>

namespace StellarAlia::Resource {

// ─── In-memory representation of a cooked animation clip (.saanim) ───────────

struct CookedAnim {
    AssetID  id;
    AnimClip clip;

    bool IsValid() const { return !clip.channels.empty(); }
};

// ─── .saanim binary layout v1 ─────────────────────────────────────────────────
//
//  FileHeader  (96 bytes)
//  ChannelHeader[channel_count]  (16 bytes each)
//  Per-channel data (sequential after headers):
//    times[keyframe_count]  × float (4 bytes)
//    values[keyframe_count] × vec4  (16 bytes)
//
namespace SaanimFormat {
    static constexpr uint32_t Magic   = 0x4D494E41u;  // 'ANIM' LE
    static constexpr uint32_t Version = 1u;

#pragma pack(push, 1)
    struct FileHeader {
        uint32_t magic;
        uint32_t version;
        uint64_t uuid_hi;
        uint64_t uuid_lo;
        float    duration;
        uint32_t channel_count;
        char     clip_name[64];
    };
    static_assert(sizeof(FileHeader) == 96);

    struct ChannelHeader {
        int32_t  bone_index;
        uint8_t  target;        // AnimChannel::Target as uint8
        uint8_t  interp;        // AnimChannel::Interp as uint8
        uint16_t _pad;
        uint32_t keyframe_count;
        uint32_t _pad2;
    };
    static_assert(sizeof(ChannelHeader) == 16);
#pragma pack(pop)
} // namespace SaanimFormat

bool SaveCookedAnim(const CookedAnim& anim, const std::string& path);
bool LoadCookedAnim(const std::string& path, CookedAnim& out);

// Derive a deterministic animation AssetID from a mesh UUID and animation index.
inline AssetID DeriveAnimID(const AssetID& meshId, int32_t animIndex) {
    const uint64_t idx = static_cast<uint64_t>(animIndex) + 1u;
    AssetID id;
    id.hi = meshId.hi ^ (idx * 0x6c62272e07bb0142ULL);
    id.lo = meshId.lo ^ (idx * 0x517cc1b727220a95ULL);
    id.hi = (id.hi & 0xFFFFFFFFFFFF0FFFull) | 0x0000000000004000ull;
    id.lo = (id.lo & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;
    return id;
}

} // namespace StellarAlia::Resource
