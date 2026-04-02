#pragma once

#include "core/asset/AssetID.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace StellarAlia::Resource {

// Describes one draw call within the cooked mesh.
// localTransform is the node world transform baked at cook time (column-major).
// v3 adds per-submesh PBR material data: texture AssetIDs and scalar factors.
struct CookedSubMesh {
    // ── Geometry (v2) ─────────────────────────────────────────────────────────
    uint32_t  vertexOffset    = 0;  // index of first vertex in the shared VB
    uint32_t  vertexCount     = 0;
    uint32_t  indexOffset     = 0;  // index of first index  in the shared IB
    uint32_t  indexCount      = 0;
    int32_t   materialIndex   = -1; // original glTF material index (-1 = none)
    uint32_t  _pad            = 0;
    glm::mat4 localTransform  = glm::mat4(1.0f);  // per-node world transform

    // ── Material (v3) ─────────────────────────────────────────────────────────
    // Texture AssetIDs — invalid (zeroed) if the glTF material has no texture.
    AssetID   baseColorTexture;
    AssetID   normalTexture;
    AssetID   metallicRoughnessTexture;
    AssetID   occlusionTexture;
    AssetID   emissiveTexture;
    // PBR scalar factors
    glm::vec4 baseColorFactor    = {1.f, 1.f, 1.f, 1.f};
    float     roughnessFactor    = 1.0f;
    float     metallicFactor     = 1.0f;
    float     normalScale        = 1.0f;
    float     occlusionStrength  = 1.0f;
    glm::vec3 emissiveFactor     = {0.f, 0.f, 0.f};
    uint32_t  _matPad            = 0;
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

// ─── .samesh binary layout (v3) ──────────────────────────────────────────────
//
//  FileHeader              (48 bytes)
//  SubMeshEntry[count]     (216 bytes each)
//  vertex data blob
//  index  data blob
//
// v2 → v3: SubMeshEntry grows from 88 → 216 bytes (adds 5 texture AssetIDs +
//           PBR scalar factors).  v2 files will fail the version check and
//           trigger automatic re-cook.
//
namespace SameshFormat {
    static constexpr uint32_t Magic   = 0x48534D53u; // 'SMSH' LE
    static constexpr uint32_t Version = 3u;          // v3 adds per-submesh material data

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
        // v2: geometry (88 bytes)
        uint32_t vertex_offset;
        uint32_t vertex_count;
        uint32_t index_offset;
        uint32_t index_count;
        int32_t  material_index;
        uint32_t _pad;
        float    local_transform[16];   // column-major glm::mat4

        // v3: material textures — 5 × AssetID (80 bytes)
        uint64_t tex_base_color_hi,          tex_base_color_lo;
        uint64_t tex_normal_hi,              tex_normal_lo;
        uint64_t tex_metallic_roughness_hi,  tex_metallic_roughness_lo;
        uint64_t tex_occlusion_hi,           tex_occlusion_lo;
        uint64_t tex_emissive_hi,            tex_emissive_lo;

        // v3: PBR scalars (48 bytes)
        float    base_color_factor[4];
        float    roughness_factor;
        float    metallic_factor;
        float    normal_scale;
        float    occlusion_strength;
        float    emissive_factor[3];
        uint32_t _mat_pad;
    };
    static_assert(sizeof(SubMeshEntry) == 216);
#pragma pack(pop)
} // namespace SameshFormat

bool SaveCookedMesh(const CookedMesh& mesh, const std::string& path);
bool LoadCookedMesh(const std::string& path, CookedMesh& out);

} // namespace StellarAlia::Resource
