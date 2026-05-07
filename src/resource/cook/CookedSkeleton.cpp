#include "CookedSkeleton.hpp"

#include <cstring>
#include <fstream>

namespace StellarAlia::Resource {

bool SaveCookedSkeleton(const CookedSkeleton& skel, const std::string& path) {
    if (!skel.IsValid()) return false;

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;

    SaskelFormat::FileHeader hdr{};
    hdr.magic      = SaskelFormat::Magic;
    hdr.version    = SaskelFormat::Version;
    hdr.uuid_hi    = skel.id.hi;
    hdr.uuid_lo    = skel.id.lo;
    hdr.bone_count = static_cast<uint32_t>(skel.bones.size());
    f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

    for (const auto& bone : skel.bones) {
        SaskelFormat::BoneEntry entry{};
        std::strncpy(entry.name, bone.name.c_str(), sizeof(entry.name) - 1);
        entry.parent_index = bone.parentIndex;
        std::memcpy(entry.inv_bind, &bone.inverseBindMatrix[0][0],
                    sizeof(entry.inv_bind));
        f.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
    }

    return f.good();
}

bool LoadCookedSkeleton(const std::string& path, CookedSkeleton& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    SaskelFormat::FileHeader hdr{};
    f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!f || hdr.magic != SaskelFormat::Magic || hdr.version != SaskelFormat::Version)
        return false;

    out.id.hi = hdr.uuid_hi;
    out.id.lo = hdr.uuid_lo;
    out.bones.resize(hdr.bone_count);

    for (auto& bone : out.bones) {
        SaskelFormat::BoneEntry entry{};
        f.read(reinterpret_cast<char*>(&entry), sizeof(entry));
        if (!f) return false;
        bone.name        = entry.name;
        bone.parentIndex = entry.parent_index;
        std::memcpy(&bone.inverseBindMatrix[0][0], entry.inv_bind,
                    sizeof(entry.inv_bind));
    }

    return true;
}

} // namespace StellarAlia::Resource
