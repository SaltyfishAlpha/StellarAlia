#include "CookedAnim.hpp"

#include <cstring>
#include <fstream>

namespace StellarAlia::Resource {

bool SaveCookedAnim(const CookedAnim& anim, const std::string& path) {
    if (!anim.IsValid()) return false;

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;

    const AnimClip& clip = anim.clip;

    SaanimFormat::FileHeader hdr{};
    hdr.magic         = SaanimFormat::Magic;
    hdr.version       = SaanimFormat::Version;
    hdr.uuid_hi       = anim.id.hi;
    hdr.uuid_lo       = anim.id.lo;
    hdr.duration      = clip.duration;
    hdr.channel_count = static_cast<uint32_t>(clip.channels.size());
    hdr.event_count   = static_cast<uint32_t>(clip.events.size());
    std::strncpy(hdr.clip_name, clip.name.c_str(), sizeof(hdr.clip_name) - 1);
    f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

    // Write all channel headers first.
    for (const auto& ch : clip.channels) {
        SaanimFormat::ChannelHeader chHdr{};
        chHdr.bone_index     = ch.boneIndex;
        chHdr.target         = static_cast<uint8_t>(ch.target);
        chHdr.interp         = static_cast<uint8_t>(ch.interp);
        chHdr.keyframe_count = static_cast<uint32_t>(ch.times.size());
        f.write(reinterpret_cast<const char*>(&chHdr), sizeof(chHdr));
    }

    // Write per-channel data (times then values).
    for (const auto& ch : clip.channels) {
        const uint32_t kf = static_cast<uint32_t>(ch.times.size());
        f.write(reinterpret_cast<const char*>(ch.times.data()),
                static_cast<std::streamsize>(kf * sizeof(float)));
        f.write(reinterpret_cast<const char*>(ch.values.data()),
                static_cast<std::streamsize>(kf * sizeof(glm::vec4)));
    }

    // v2 event block — length-prefixed strings.
    auto writeStr = [&](const std::string& s) {
        const uint32_t len = static_cast<uint32_t>(s.size());
        f.write(reinterpret_cast<const char*>(&len), sizeof(len));
        if (len) f.write(s.data(), static_cast<std::streamsize>(len));
    };
    for (const auto& ev : clip.events) {
        f.write(reinterpret_cast<const char*>(&ev.time), sizeof(ev.time));
        writeStr(ev.name);
        writeStr(ev.payload);
    }

    return f.good();
}

bool LoadCookedAnim(const std::string& path, CookedAnim& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    SaanimFormat::FileHeader hdr{};
    f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!f || hdr.magic != SaanimFormat::Magic ||
        hdr.version < 1u || hdr.version > SaanimFormat::Version)
        return false;
    // v1 had no event_count field (that word was inside clip_name[64]); force 0.
    if (hdr.version < 2u) hdr.event_count = 0u;

    out.id.hi = hdr.uuid_hi;
    out.id.lo = hdr.uuid_lo;

    AnimClip& clip  = out.clip;
    clip.name       = hdr.clip_name;
    clip.duration   = hdr.duration;
    clip.channels.resize(hdr.channel_count);

    // Read channel headers.
    std::vector<uint32_t> kfCounts(hdr.channel_count);
    for (uint32_t i = 0; i < hdr.channel_count; ++i) {
        SaanimFormat::ChannelHeader chHdr{};
        f.read(reinterpret_cast<char*>(&chHdr), sizeof(chHdr));
        if (!f) return false;

        auto& ch    = clip.channels[i];
        ch.boneIndex = chHdr.bone_index;
        ch.target    = static_cast<AnimChannel::Target>(chHdr.target);
        ch.interp    = static_cast<AnimChannel::Interp>(chHdr.interp);
        kfCounts[i]  = chHdr.keyframe_count;
    }

    // Read per-channel data.
    for (uint32_t i = 0; i < hdr.channel_count; ++i) {
        auto& ch        = clip.channels[i];
        const uint32_t kf = kfCounts[i];
        ch.times.resize(kf);
        ch.values.resize(kf);
        f.read(reinterpret_cast<char*>(ch.times.data()),
               static_cast<std::streamsize>(kf * sizeof(float)));
        f.read(reinterpret_cast<char*>(ch.values.data()),
               static_cast<std::streamsize>(kf * sizeof(glm::vec4)));
        if (!f) return false;
    }

    // v2 event block.
    auto readStr = [&](std::string& s) -> bool {
        uint32_t len = 0;
        f.read(reinterpret_cast<char*>(&len), sizeof(len));
        if (!f || len > (1u << 20)) return false;   // sanity cap
        s.resize(len);
        if (len) f.read(s.data(), static_cast<std::streamsize>(len));
        return static_cast<bool>(f);
    };
    clip.events.resize(hdr.event_count);
    for (uint32_t i = 0; i < hdr.event_count; ++i) {
        auto& ev = clip.events[i];
        f.read(reinterpret_cast<char*>(&ev.time), sizeof(ev.time));
        if (!f || !readStr(ev.name) || !readStr(ev.payload)) return false;
    }

    return true;
}

} // namespace StellarAlia::Resource
