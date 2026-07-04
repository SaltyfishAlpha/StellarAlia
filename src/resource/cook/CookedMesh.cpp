#include "CookedMesh.hpp"

#include <cstring>
#include <fstream>

namespace StellarAlia::Resource {

bool SaveCookedMesh(const CookedMesh& mesh, const std::string& path) {
    if (!mesh.IsValid()) return false;

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;

    SameshFormat::FileHeader hdr{};
    hdr.magic          = SameshFormat::Magic;
    hdr.version        = SameshFormat::Version;
    hdr.uuid_hi        = mesh.id.hi;
    hdr.uuid_lo        = mesh.id.lo;
    hdr.vertex_count   = mesh.vertexCount;
    hdr.index_count    = mesh.indexCount;
    hdr.vertex_stride  = mesh.vertexStride;
    hdr.index_stride   = mesh.indexStride;
    hdr.submesh_count  = static_cast<uint32_t>(mesh.subMeshes.size());
    hdr.skin_data_size = static_cast<uint32_t>(mesh.skinData.size());

    f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

    for (const auto& sm : mesh.subMeshes) {
        SameshFormat::SubMeshEntry entry{};
        entry.vertex_offset  = sm.vertexOffset;
        entry.vertex_count   = sm.vertexCount;
        entry.index_offset   = sm.indexOffset;
        entry.index_count    = sm.indexCount;
        entry.material_index = sm.materialIndex;
        std::memcpy(entry.local_transform, &sm.localTransform[0][0],
                    sizeof(entry.local_transform));
        entry.default_mat_hi = sm.defaultMaterialID.hi;
        entry.default_mat_lo = sm.defaultMaterialID.lo;
        f.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
    }

    f.write(reinterpret_cast<const char*>(mesh.vertexData.data()),
            static_cast<std::streamsize>(mesh.vertexData.size()));
    f.write(reinterpret_cast<const char*>(mesh.indexData.data()),
            static_cast<std::streamsize>(mesh.indexData.size()));

    if (!mesh.skinData.empty())
        f.write(reinterpret_cast<const char*>(mesh.skinData.data()),
                static_cast<std::streamsize>(mesh.skinData.size()));

    // v6: material name table — always submesh_count entries (missing names = empty)
    const uint32_t nameCount = hdr.submesh_count;
    f.write(reinterpret_cast<const char*>(&nameCount), sizeof(nameCount));
    for (uint32_t i = 0; i < nameCount; ++i) {
        const std::string& name =
            i < mesh.materialNames.size() ? mesh.materialNames[i] : std::string{};
        const uint32_t len = static_cast<uint32_t>(name.size());
        f.write(reinterpret_cast<const char*>(&len), sizeof(len));
        f.write(name.data(), static_cast<std::streamsize>(len));
    }

    return f.good();
}

bool LoadCookedMesh(const std::string& path, CookedMesh& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    SameshFormat::FileHeader hdr{};
    f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!f || hdr.magic != SameshFormat::Magic ||
        hdr.version < SameshFormat::MinVersion || hdr.version > SameshFormat::Version)
        return false;

    out.id.hi         = hdr.uuid_hi;
    out.id.lo         = hdr.uuid_lo;
    out.vertexCount   = hdr.vertex_count;
    out.indexCount    = hdr.index_count;
    out.vertexStride  = hdr.vertex_stride;
    out.indexStride   = hdr.index_stride;

    out.subMeshes.resize(hdr.submesh_count);
    for (auto& sm : out.subMeshes) {
        SameshFormat::SubMeshEntry entry{};
        f.read(reinterpret_cast<char*>(&entry), sizeof(entry));
        if (!f) return false;
        sm.vertexOffset  = entry.vertex_offset;
        sm.vertexCount   = entry.vertex_count;
        sm.indexOffset   = entry.index_offset;
        sm.indexCount    = entry.index_count;
        sm.materialIndex = entry.material_index;
        std::memcpy(&sm.localTransform[0][0], entry.local_transform,
                    sizeof(entry.local_transform));
        sm.defaultMaterialID.hi = entry.default_mat_hi;
        sm.defaultMaterialID.lo = entry.default_mat_lo;
    }

    const size_t vbSize = static_cast<size_t>(out.vertexCount) * out.vertexStride;
    const size_t ibSize = static_cast<size_t>(out.indexCount)  * out.indexStride;

    out.vertexData.resize(vbSize);
    out.indexData.resize(ibSize);

    f.read(reinterpret_cast<char*>(out.vertexData.data()), static_cast<std::streamsize>(vbSize));
    f.read(reinterpret_cast<char*>(out.indexData.data()),  static_cast<std::streamsize>(ibSize));

    if (hdr.skin_data_size > 0) {
        out.skinData.resize(hdr.skin_data_size);
        f.read(reinterpret_cast<char*>(out.skinData.data()),
               static_cast<std::streamsize>(hdr.skin_data_size));
    }

    out.materialNames.clear();
    if (hdr.version >= 6) {
        uint32_t nameCount = 0;
        f.read(reinterpret_cast<char*>(&nameCount), sizeof(nameCount));
        if (!f || nameCount != hdr.submesh_count) return false;
        out.materialNames.resize(nameCount);
        for (auto& name : out.materialNames) {
            uint32_t len = 0;
            f.read(reinterpret_cast<char*>(&len), sizeof(len));
            if (!f || len > 4096) return false;  // sanity cap against corrupt files
            name.resize(len);
            if (len) f.read(name.data(), static_cast<std::streamsize>(len));
        }
    }

    return f.good() || f.eof();
}

} // namespace StellarAlia::Resource
