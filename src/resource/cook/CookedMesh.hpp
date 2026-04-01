#pragma once

#include "core/asset/AssetID.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace StellarAlia::Resource {

// Describes one draw call within the cooked mesh.
// material_index is the original glTF material index (-1 = none).
// In Stage 3.5 this will be replaced by a material AssetID.
struct CookedSubMesh {
    uint32_t vertexOffset   = 0;  // index of first vertex in the shared VB
    uint32_t vertexCount    = 0;
    uint32_t indexOffset    = 0;  // index of first index  in the shared IB
    uint32_t indexCount     = 0;
    int32_t  materialIndex  = -1;
    uint32_t _pad           = 0;
};

// In-memory representation loaded from a .samesh file.
// Vertex layout (48 bytes per vertex, matches MeshData::Vertex):
//   vec3 position  (12 bytes, location = 0)
//   vec3 normal    (12 bytes, location = 1)
//   vec4 tangent   (16 bytes, location = 2, w = handedness)
//   vec2 texCoord0 ( 8 bytes, location = 3)
struct CookedMesh {
    AssetID  id;
    uint32_t vertexCount  = 0;
    uint32_t indexCount   = 0;
    uint32_t vertexStride = 48;   // bytes per vertex (fixed for current layout)
    uint32_t indexStride  = 4;    // bytes per index  (uint32_t)

    std::vector<CookedSubMesh> subMeshes;
    std::vector<uint8_t>       vertexData;  // vertexCount * vertexStride bytes
    std::vector<uint8_t>       indexData;   // indexCount  * indexStride  bytes

    bool IsValid() const { return vertexCount > 0 && !vertexData.empty(); }
};

// ─── .samesh binary layout ───────────────────────────────────────────────────
//
//  FileHeader              (48 bytes)
//  SubMeshEntry[count]     (24 bytes each)
//  vertex data blob
//  index  data blob
//
namespace SameshFormat {
    static constexpr uint32_t Magic   = 0x48534D53u; // 'SMSH' LE
    static constexpr uint32_t Version = 1u;

#pragma pack(push, 1)
    struct FileHeader {
        uint32_t magic;
        uint32_t version;
        uint64_t uuid_hi;
        uint64_t uuid_lo;
        uint32_t vertex_count;
        uint32_t index_count;
        uint32_t vertex_stride;
        uint32_t index_stride;
        uint32_t submesh_count;
        uint32_t _pad;
    };
    static_assert(sizeof(FileHeader) == 48);

    struct SubMeshEntry {
        uint32_t vertex_offset;
        uint32_t vertex_count;
        uint32_t index_offset;
        uint32_t index_count;
        int32_t  material_index;
        uint32_t _pad;
    };
    static_assert(sizeof(SubMeshEntry) == 24);
#pragma pack(pop)
} // namespace SameshFormat

bool SaveCookedMesh(const CookedMesh& mesh, const std::string& path);
bool LoadCookedMesh(const std::string& path, CookedMesh& out);

} // namespace StellarAlia::Resource
