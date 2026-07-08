#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include "resource/loaders/ObjLoader.hpp"
#include "resource/loaders/MeshUtils.hpp"
#include "core/logs/Log.hpp"

#include <cmath>
#include <filesystem>
#include <unordered_map>

namespace StellarAlia::Resource {

namespace {

// One (v, vn, vt) index triple = one engine vertex; OBJ indexes streams
// independently so identical triples are welded here.
struct IndexKey {
    int v, n, t;
    bool operator==(const IndexKey& o) const { return v == o.v && n == o.n && t == o.t; }
};
struct IndexKeyHash {
    size_t operator()(const IndexKey& k) const {
        size_t h = std::hash<int>{}(k.v);
        h ^= std::hash<int>{}(k.n) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(k.t) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

int32_t AddImage(SceneData& scene, std::unordered_map<std::string, int32_t>& table,
                 const std::string& texname) {
    if (texname.empty()) return -1;
    if (auto it = table.find(texname); it != table.end()) return it->second;

    ImageData img;
    img.path = texname;  // relative to the .obj — resolved by the cook side
    const auto idx = static_cast<int32_t>(scene.images.size());
    scene.images.push_back(std::move(img));
    table.emplace(texname, idx);
    return idx;
}

MaterialData ConvertMaterial(SceneData& scene,
                             std::unordered_map<std::string, int32_t>& imageTable,
                             const tinyobj::material_t& m) {
    MaterialData out;
    out.name = m.name;

    out.baseColorFactor = {m.diffuse[0], m.diffuse[1], m.diffuse[2],
                           m.dissolve > 0.f ? m.dissolve : 1.f};
    out.emissiveFactor  = {m.emission[0], m.emission[1], m.emission[2]};

    // Pr/Pm (MTL PBR extension) win; otherwise derive roughness from the
    // Blinn-Phong exponent (Ns=0 in exports usually means "unset" → rough).
    out.metallicFactor  = m.metallic;
    out.roughnessFactor = m.roughness > 0.f ? m.roughness
                         : m.shininess > 0.f
                             ? std::sqrt(2.f / (2.f + m.shininess))
                             : 1.f;

    out.baseColorTexture.imageIndex = AddImage(scene, imageTable, m.diffuse_texname);
    out.emissiveTexture.imageIndex  = AddImage(scene, imageTable, m.emissive_texname);
    const std::string& normalTex = !m.normal_texname.empty() ? m.normal_texname
                                                             : m.bump_texname;
    out.normalTexture.imageIndex = AddImage(scene, imageTable, normalTex);

    if (m.dissolve < 1.f && m.dissolve > 0.f)
        out.alphaMode = "BLEND";
    return out;
}

} // namespace

std::optional<SceneData> ObjLoader::Load(const std::string& path) {
    tinyobj::ObjReaderConfig cfg;
    cfg.triangulate     = true;
    cfg.mtl_search_path = std::filesystem::path(path).parent_path().string();

    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(path, cfg)) {
        SA_LOG_ERROR("ObjLoader: failed to parse '{}': {}", path, reader.Error());
        return std::nullopt;
    }
    if (!reader.Warning().empty())
        SA_LOG_WARN("ObjLoader: '{}': {}", path, reader.Warning());

    const auto& attrib = reader.GetAttrib();
    const auto& shapes = reader.GetShapes();

    SceneData scene;
    scene.sourcePath = path;

    std::unordered_map<std::string, int32_t> imageTable;
    for (const auto& m : reader.GetMaterials())
        scene.materials.push_back(ConvertMaterial(scene, imageTable, m));

    MeshData mesh;
    mesh.name = std::filesystem::path(path).stem().string();

    for (const auto& shape : shapes) {
        // split the shape into one primitive per material id
        std::unordered_map<int, size_t> primOfMaterial;  // material id → mesh.primitives idx
        std::unordered_map<int, std::unordered_map<IndexKey, uint32_t, IndexKeyHash>> weld;
        std::unordered_map<int, bool> primHasNormals, primHasUVs;

        const size_t faceCount = shape.mesh.indices.size() / 3;
        for (size_t f = 0; f < faceCount; ++f) {
            const int matId = f < shape.mesh.material_ids.size()
                                  ? shape.mesh.material_ids[f] : -1;

            auto [pit, isNew] = primOfMaterial.try_emplace(matId, mesh.primitives.size());
            if (isNew) {
                Primitive prim;
                prim.materialIndex = matId;
                mesh.primitives.push_back(std::move(prim));
                primHasNormals[matId] = true;
                primHasUVs[matId]     = true;
            }
            Primitive& prim = mesh.primitives[pit->second];
            auto&      dedup = weld[matId];

            for (int c = 0; c < 3; ++c) {
                const tinyobj::index_t& idx = shape.mesh.indices[f * 3 + c];
                const IndexKey key{idx.vertex_index, idx.normal_index, idx.texcoord_index};

                auto [vit, inserted] = dedup.try_emplace(
                    key, static_cast<uint32_t>(prim.vertices.size()));
                if (inserted) {
                    Vertex v;
                    if (idx.vertex_index >= 0) {
                        const auto* p = &attrib.vertices[3 * idx.vertex_index];
                        v.position = {p[0], p[1], p[2]};
                    }
                    if (idx.normal_index >= 0) {
                        const auto* n = &attrib.normals[3 * idx.normal_index];
                        v.normal = {n[0], n[1], n[2]};
                    } else {
                        primHasNormals[matId] = false;
                    }
                    if (idx.texcoord_index >= 0) {
                        const auto* t = &attrib.texcoords[2 * idx.texcoord_index];
                        // OBJ UV origin is bottom-left; engine follows glTF (top-left)
                        v.texCoord0 = {t[0], 1.f - t[1]};
                    } else {
                        primHasUVs[matId] = false;
                    }
                    prim.vertices.push_back(v);
                }
                prim.indices.push_back(vit->second);
            }
        }

        for (auto& [matId, primIdx] : primOfMaterial) {
            Primitive& prim = mesh.primitives[primIdx];
            // Missing vn → faceted normals: OBJ welds by (v,vt), so smooth
            // accumulation would average across hard edges wherever two faces
            // share a corner. GenerateTangents re-welds the split verts after.
            if (!primHasNormals[matId])
                MeshUtils::GenerateFlatNormals(prim.vertices, prim.indices);
            if (primHasUVs[matId])
                MeshUtils::GenerateTangents(prim.vertices, prim.indices);
        }
    }

    if (mesh.primitives.empty()) {
        SA_LOG_ERROR("ObjLoader: '{}' contains no triangle geometry", path);
        return std::nullopt;
    }

    scene.meshes.push_back(std::move(mesh));

    SceneNode root;
    root.name      = scene.meshes[0].name;
    root.meshIndex = 0;
    scene.nodes.push_back(std::move(root));
    scene.rootNodes.push_back(0);

    SA_LOG_INFO("ObjLoader: '{}' — {} primitives, {} materials, {} verts",
                path, scene.meshes[0].primitives.size(), scene.materials.size(),
                scene.TotalVertexCount());
    return scene;
}

} // namespace StellarAlia::Resource
