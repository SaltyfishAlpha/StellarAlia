#pragma once

#include "core/asset/AssetID.hpp"
#include "resource/types/AnimData.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace StellarAlia::Resource {

// Describes one draw call within the cooked mesh.
// localTransform is the node world transform baked at cook time (column-major).
// v4: material data lives in a separate .samat asset; submesh only stores a
//     reference (defaultMaterialID) to the corresponding .samat file.
struct CookedSubMesh {
    // ── Geometry ──────────────────────────────────────────────────────────────
    uint32_t  vertexOffset    = 0;  // index of first vertex in the shared VB
    uint32_t  vertexCount     = 0;
    uint32_t  indexOffset     = 0;  // index of first index  in the shared IB
    uint32_t  indexCount      = 0;
    int32_t   materialIndex   = -1; // original glTF material index (-1 = none)
    uint32_t  _pad            = 0;
    glm::mat4 localTransform  = glm::mat4(1.0f);  // per-node world transform

    // ── Material reference (v4) ───────────────────────────────────────────────
    // UUID of the .samat asset cooked from the glTF material at this slot.
    // Invalid (zeroed) when materialIndex == -1.
    AssetID   defaultMaterialID;
};

// In-memory representation loaded from a .samesh file.
// Vertex layout (48 bytes per vertex, matches MeshData::Vertex):
//   vec3 position  (12 bytes, location = 0)
//   vec3 normal    (12 bytes, location = 1)
//   vec4 tangent   (16 bytes, location = 2, w = handedness)
//   vec2 texCoord0 ( 8 bytes, location = 3)
//
// Skinning data (v5+, optional): SkinVertex[vertexCount] parallel to vertexData.
//   SkinVertex = uvec4 joints (16 bytes) + vec4 weights (16 bytes) = 32 bytes each.
struct CookedMesh {
    AssetID  id;
    uint32_t vertexCount  = 0;
    uint32_t indexCount   = 0;
    uint32_t vertexStride = 48;   // bytes per vertex (fixed for current layout)
    uint32_t indexStride  = 4;    // bytes per index  (uint32_t)

    std::vector<CookedSubMesh> subMeshes;
    std::vector<uint8_t>       vertexData;  // vertexCount * vertexStride bytes
    std::vector<uint8_t>       indexData;   // indexCount  * indexStride  bytes
    std::vector<uint8_t>       skinData;    // vertexCount * sizeof(SkinVertex) bytes; empty = static

    bool IsValid()    const { return vertexCount > 0 && !vertexData.empty(); }
    bool IsSkinned()  const { return !skinData.empty(); }
};

// ─── .samesh binary layout (v5) ──────────────────────────────────────────────
//
//  FileHeader              (48 bytes)
//  SubMeshEntry[count]     (104 bytes each)
//  vertex data blob
//  index  data blob
//  skin   data blob        (only if skin_data_size > 0; vertexCount × 32 bytes)
//
// v4 → v5: FileHeader._pad replaced by skin_data_size (0 = static mesh).
//           Skin data blob appended after index blob.
//
namespace SameshFormat {
    static constexpr uint32_t Magic   = 0x48534D53u; // 'SMSH' LE
    static constexpr uint32_t Version = 5u;          // v5: optional skin data blob

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
        uint32_t skin_data_size;  // bytes of SkinVertex data after index blob; 0 = no skinning
    };
    static_assert(sizeof(FileHeader) == 48);

    struct SubMeshEntry {
        // Geometry (88 bytes, unchanged since v2)
        uint32_t vertex_offset;
        uint32_t vertex_count;
        uint32_t index_offset;
        uint32_t index_count;
        int32_t  material_index;
        uint32_t _pad;
        float    local_transform[16];   // column-major glm::mat4

        // v4: default material reference (16 bytes)
        uint64_t default_mat_hi;
        uint64_t default_mat_lo;
    };
    static_assert(sizeof(SubMeshEntry) == 104);
#pragma pack(pop)
} // namespace SameshFormat

bool SaveCookedMesh(const CookedMesh& mesh, const std::string& path);
bool LoadCookedMesh(const std::string& path, CookedMesh& out);

// ─── Per-node mesh ID derivation ─────────────────────────────────────────────
//
// When a glTF is cooked with the per-node split (static, non-skinned), each
// node that carries geometry gets its own .samesh.  The ID is deterministically
// derived from the glTF file's AssetID and the node index so that scene files
// can reference per-node meshes by a stable UUID without extra metadata.
//
// NOTE: nodeIdx is the node's index in SceneData::nodes (0-based).
inline AssetID DeriveNodeMeshID(const AssetID& fileId, uint32_t nodeIdx) {
    // High offset (0x10000) avoids collision with image IDs (1-based) and
    // material IDs (also low indices) that also derive from the same fileId.
    const uint64_t n = static_cast<uint64_t>(nodeIdx) + 0x10000u;
    AssetID id;
    id.hi = fileId.hi ^ (n * 0x517cc1b727220a95ULL);
    id.lo = fileId.lo ^ (n * 0x6c62272e07bb0142ULL);
    // Stamp as UUID v4 / variant 1 (RFC 4122).
    id.hi = (id.hi & 0xFFFFFFFFFFFF0FFFull) | 0x0000000000004000ull;
    id.lo = (id.lo & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;
    return id;
}

} // namespace StellarAlia::Resource
