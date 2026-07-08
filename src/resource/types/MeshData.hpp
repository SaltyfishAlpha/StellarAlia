#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "resource/types/ImageData.hpp"
#include "resource/types/AnimData.hpp"

namespace StellarAlia::Resource {

// ─────────────────────────────────────────────────────────────────────────────
// Vertex  —  matches pbr.vert attribute layout exactly.
//   location 0  vec3  position
//   location 1  vec3  normal
//   location 2  vec4  tangent   (w = handedness ±1, per glTF spec)
//   location 3  vec2  texCoord0
// ─────────────────────────────────────────────────────────────────────────────
struct Vertex {
    glm::vec3 position  = {0, 0, 0};
    glm::vec3 normal    = {0, 1, 0};
    glm::vec4 tangent   = {1, 0, 0, 1};
    glm::vec2 texCoord0 = {0, 0};
};

// ─────────────────────────────────────────────────────────────────────────────
// MaterialData  —  PBR parameters mirroring pbr.frag set=1 bindings.
// ─────────────────────────────────────────────────────────────────────────────
struct TextureRef {
    int32_t imageIndex = -1;   // index into SceneData::images (-1 = none)
    std::string uri;           // file URI for external textures
};

struct MaterialData {
    std::string name;

    // UBO params (matching u_Mat in pbr.frag)
    glm::vec4 baseColorFactor      = {1, 1, 1, 1};
    float     roughnessFactor      = 1.0f;
    float     metallicFactor       = 1.0f;
    float     normalScale          = 1.0f;
    float     occlusionStrength    = 1.0f;
    glm::vec3 emissiveFactor       = {0, 0, 0};

    // Texture refs (binding slots matching pbr.frag)
    TextureRef baseColorTexture;         // binding 1
    TextureRef normalTexture;            // binding 2
    TextureRef metallicRoughnessTexture; // binding 3
    TextureRef occlusionTexture;         // binding 4
    TextureRef emissiveTexture;          // binding 5

    bool        doubleSided = false;
    std::string alphaMode   = "OPAQUE";  // OPAQUE | MASK | BLEND
    float       alphaCutoff = 0.5f;

    // Issue #108 — pass-through for non-PBR shading models (MToon, MMDToon…).
    // A loader sets shadingModel + format-specific params/texture slots and
    // CookMaterial writes them into the .samatc JSON verbatim. Loading is
    // reflection-driven against the registered MaterialType and unknown keys
    // are ignored, so nothing downstream needs to know the format.
    std::string                               shadingModel = "PBR";
    std::map<std::string, std::vector<float>> extraParams;    // 1=scalar, 2-4=vecN
    std::map<std::string, TextureRef>         extraTextures;  // slot name → image
};

// ─────────────────────────────────────────────────────────────────────────────
// Primitive  —  one draw call's worth of geometry + material.
// ─────────────────────────────────────────────────────────────────────────────
struct Primitive {
    std::vector<Vertex>      vertices;
    std::vector<uint32_t>    indices;
    std::vector<SkinVertex>  skinVertices;  // parallel to vertices; empty if not skinned
    int32_t                  materialIndex = -1;  // index into SceneData::materials
    int32_t                  skinIndex     = -1;  // index into SceneData::skins; -1 = unskinned
};

// ─────────────────────────────────────────────────────────────────────────────
// MeshData  —  a named collection of primitives.
// ─────────────────────────────────────────────────────────────────────────────
struct MeshData {
    std::string            name;
    std::vector<Primitive> primitives;
};

// ─────────────────────────────────────────────────────────────────────────────
// SkeletonData  —  one glTF skin (bone hierarchy + inverse bind matrices).
// ─────────────────────────────────────────────────────────────────────────────
struct SkeletonData {
    std::string           name;
    std::vector<BoneInfo> bones;  // indexed by joint index used in SkinVertex::joints
};

// ─────────────────────────────────────────────────────────────────────────────
// SceneNode  —  node in the glTF scene hierarchy.
// ─────────────────────────────────────────────────────────────────────────────
struct SceneNode {
    std::string           name;
    glm::mat4             localTransform = glm::mat4(1.0f);
    int32_t               meshIndex      = -1;   // -1 = no mesh
    int32_t               skinIndex      = -1;   // index into SceneData::skins; -1 = no skin
    std::vector<uint32_t> children;
};

// ─────────────────────────────────────────────────────────────────────────────
// SceneData  —  complete CPU-side scene loaded from a single glTF/GLB file.
// ─────────────────────────────────────────────────────────────────────────────
struct SceneData {
    std::string               sourcePath;
    std::vector<MeshData>     meshes;
    std::vector<MaterialData> materials;
    std::vector<ImageData>    images;      // embedded / referenced textures
    std::vector<SceneNode>    nodes;
    std::vector<uint32_t>     rootNodes;
    std::vector<SkeletonData> skins;       // one per glTF skin
    std::vector<AnimClip>     animations;  // one per glTF animation

    [[nodiscard]] size_t TotalVertexCount() const noexcept {
        size_t n = 0;
        for (auto& m : meshes)
            for (auto& p : m.primitives)
                n += p.vertices.size();
        return n;
    }
    [[nodiscard]] size_t TotalIndexCount() const noexcept {
        size_t n = 0;
        for (auto& m : meshes)
            for (auto& p : m.primitives)
                n += p.indices.size();
        return n;
    }
};

} // namespace StellarAlia::Resource
