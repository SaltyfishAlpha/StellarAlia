// tinygltf implementation — defined in exactly one translation unit.
// TINYGLTF_NO_STB_IMAGE prevents tinygltf from re-defining STB_IMAGE_IMPLEMENTATION
// (already owned by StbImpl.cpp). We supply a custom image loader callback instead.
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_USE_CPP14
#include <tiny_gltf.h>

#include "resource/loaders/GltfLoader.hpp"
#include "resource/loaders/ImageLoader.hpp"
#include "core/logs/Log.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace StellarAlia::Resource {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers: accessor → typed span
// ─────────────────────────────────────────────────────────────────────────────
namespace {

// Returns a pointer to the start of an accessor's data, or nullptr.
template<typename T>
const T* AccessorData(const tinygltf::Model& model, int accessorIdx) {
    if (accessorIdx < 0) return nullptr;
    const auto& acc    = model.accessors[accessorIdx];
    const auto& bv     = model.bufferViews[acc.bufferView];
    const auto& buf    = model.buffers[bv.buffer];
    return reinterpret_cast<const T*>(
        buf.data.data() + bv.byteOffset + acc.byteOffset);
}

size_t AccessorCount(const tinygltf::Model& model, int accessorIdx) {
    if (accessorIdx < 0) return 0;
    return model.accessors[accessorIdx].count;
}

// Build a flat-normal for every triangle (fallback when NORMAL is absent).
void ComputeFlatNormals(std::vector<Vertex>& verts, const std::vector<uint32_t>& idx) {
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        auto& v0 = verts[idx[i]];
        auto& v1 = verts[idx[i + 1]];
        auto& v2 = verts[idx[i + 2]];
        glm::vec3 n = glm::normalize(
            glm::cross(v1.position - v0.position, v2.position - v0.position));
        v0.normal = v1.normal = v2.normal = n;
    }
}

// ── Node transform ───────────────────────────────────────────────────────────
glm::mat4 NodeLocalTransform(const tinygltf::Node& node) {
    if (node.matrix.size() == 16) {
        // Column-major matrix provided directly
        return glm::make_mat4(node.matrix.data());
    }

    glm::mat4 T(1), R(1), S(1);
    if (node.translation.size() == 3)
        T = glm::translate(glm::mat4(1.0f),
            glm::vec3((float)node.translation[0],
                      (float)node.translation[1],
                      (float)node.translation[2]));
    if (node.rotation.size() == 4) {
        // glTF quaternion: x y z w
        glm::quat q((float)node.rotation[3],
                    (float)node.rotation[0],
                    (float)node.rotation[1],
                    (float)node.rotation[2]);
        R = glm::mat4_cast(q);
    }
    if (node.scale.size() == 3)
        S = glm::scale(glm::mat4(1.0f),
            glm::vec3((float)node.scale[0],
                      (float)node.scale[1],
                      (float)node.scale[2]));
    return T * R * S;
}

// ── Primitive conversion ─────────────────────────────────────────────────────
Primitive ConvertPrimitive(const tinygltf::Model& model,
                            const tinygltf::Primitive& prim) {
    Primitive out;
    out.materialIndex = prim.material;

    // ── Positions (required) ─────────────────────────────────────────────────
    auto posIt = prim.attributes.find("POSITION");
    if (posIt == prim.attributes.end()) {
        SA_LOG_WARN("GltfLoader: primitive has no POSITION accessor, skipping");
        return out;
    }
    const size_t vertCount = AccessorCount(model, posIt->second);
    const auto* pos = AccessorData<glm::vec3>(model, posIt->second);

    out.vertices.resize(vertCount);
    for (size_t i = 0; i < vertCount; i++)
        out.vertices[i].position = pos[i];

    // ── Normals ──────────────────────────────────────────────────────────────
    auto normIt = prim.attributes.find("NORMAL");
    bool hasNormals = (normIt != prim.attributes.end());
    if (hasNormals) {
        const auto* nrm = AccessorData<glm::vec3>(model, normIt->second);
        for (size_t i = 0; i < vertCount; i++)
            out.vertices[i].normal = nrm[i];
    }

    // ── Tangents ─────────────────────────────────────────────────────────────
    auto tanIt = prim.attributes.find("TANGENT");
    if (tanIt != prim.attributes.end()) {
        const auto* tan = AccessorData<glm::vec4>(model, tanIt->second);
        for (size_t i = 0; i < vertCount; i++)
            out.vertices[i].tangent = tan[i];
    }

    // ── TexCoord_0 ────────────────────────────────────────────────────────────
    auto uvIt = prim.attributes.find("TEXCOORD_0");
    if (uvIt != prim.attributes.end()) {
        const auto& acc = model.accessors[uvIt->second];
        const auto& bv  = model.bufferViews[acc.bufferView];
        const auto& buf = model.buffers[bv.buffer];
        const uint8_t* raw = buf.data.data() + bv.byteOffset + acc.byteOffset;

        for (size_t i = 0; i < vertCount; i++) {
            if (acc.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
                const float* uv = reinterpret_cast<const float*>(raw) + i * 2;
                out.vertices[i].texCoord0 = {uv[0], uv[1]};
            } else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                const uint16_t* uv = reinterpret_cast<const uint16_t*>(raw) + i * 2;
                out.vertices[i].texCoord0 = {uv[0] / 65535.0f, uv[1] / 65535.0f};
            } else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                const uint8_t* uv = raw + i * 2;
                out.vertices[i].texCoord0 = {uv[0] / 255.0f, uv[1] / 255.0f};
            }
        }
    }

    // ── Indices ───────────────────────────────────────────────────────────────
    if (prim.indices >= 0) {
        const auto& acc = model.accessors[prim.indices];
        const auto& bv  = model.bufferViews[acc.bufferView];
        const auto& buf = model.buffers[bv.buffer];
        const uint8_t* raw = buf.data.data() + bv.byteOffset + acc.byteOffset;

        out.indices.resize(acc.count);
        for (size_t i = 0; i < acc.count; i++) {
            if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
                out.indices[i] = reinterpret_cast<const uint32_t*>(raw)[i];
            else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                out.indices[i] = reinterpret_cast<const uint16_t*>(raw)[i];
            else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
                out.indices[i] = raw[i];
        }
    } else {
        // Non-indexed: generate sequential indices
        out.indices.resize(vertCount);
        for (size_t i = 0; i < vertCount; i++)
            out.indices[i] = static_cast<uint32_t>(i);
    }

    // ── Fallback normals (flat) ───────────────────────────────────────────────
    if (!hasNormals) {
        SA_LOG_WARN("GltfLoader: primitive missing NORMAL, computing flat normals");
        ComputeFlatNormals(out.vertices, out.indices);
    }

    return out;
}

// ── Material conversion ───────────────────────────────────────────────────────
// In glTF, materials reference *textures* (not images directly).
// model.textures[texIdx].source gives the actual image index in model.images[].
MaterialData ConvertMaterial(const tinygltf::Material& mat,
                             const tinygltf::Model&    model) {
    MaterialData out;
    out.name        = mat.name;
    out.doubleSided = mat.doubleSided;
    out.alphaMode   = mat.alphaMode;
    out.alphaCutoff = static_cast<float>(mat.alphaCutoff);

    const auto& pbr = mat.pbrMetallicRoughness;

    if (pbr.baseColorFactor.size() == 4)
        out.baseColorFactor = {(float)pbr.baseColorFactor[0],
                               (float)pbr.baseColorFactor[1],
                               (float)pbr.baseColorFactor[2],
                               (float)pbr.baseColorFactor[3]};
    out.roughnessFactor = static_cast<float>(pbr.roughnessFactor);
    out.metallicFactor  = static_cast<float>(pbr.metallicFactor);

    // Resolve glTF texture index → image index via model.textures[].source
    auto texImage = [&](int texIdx) -> int32_t {
        if (texIdx < 0 || texIdx >= (int)model.textures.size()) return -1;
        return model.textures[texIdx].source;
    };

    if (mat.normalTexture.index >= 0) {
        out.normalTexture.imageIndex = texImage(mat.normalTexture.index);
        out.normalScale = static_cast<float>(mat.normalTexture.scale);
    }
    if (mat.occlusionTexture.index >= 0) {
        out.occlusionTexture.imageIndex = texImage(mat.occlusionTexture.index);
        out.occlusionStrength = static_cast<float>(mat.occlusionTexture.strength);
    }
    if (mat.emissiveTexture.index >= 0)
        out.emissiveTexture.imageIndex = texImage(mat.emissiveTexture.index);
    if (mat.emissiveFactor.size() == 3)
        out.emissiveFactor = {(float)mat.emissiveFactor[0],
                              (float)mat.emissiveFactor[1],
                              (float)mat.emissiveFactor[2]};

    if (pbr.baseColorTexture.index >= 0)
        out.baseColorTexture.imageIndex = texImage(pbr.baseColorTexture.index);
    if (pbr.metallicRoughnessTexture.index >= 0)
        out.metallicRoughnessTexture.imageIndex =
            texImage(pbr.metallicRoughnessTexture.index);

    return out;
}

// ── Node conversion (recursive via stack) ────────────────────────────────────
void ConvertNodes(const tinygltf::Model& model, SceneData& scene) {
    scene.nodes.resize(model.nodes.size());
    for (size_t i = 0; i < model.nodes.size(); i++) {
        const auto& gn = model.nodes[i];
        auto& sn       = scene.nodes[i];
        sn.name            = gn.name;
        sn.localTransform  = NodeLocalTransform(gn);
        sn.meshIndex       = gn.mesh;
        sn.children.reserve(gn.children.size());
        for (int c : gn.children)
            sn.children.push_back(static_cast<uint32_t>(c));
    }

    // Root nodes from the default scene (or first scene)
    int sceneIdx = model.defaultScene >= 0 ? model.defaultScene : 0;
    if (!model.scenes.empty()) {
        for (int r : model.scenes[sceneIdx].nodes)
            scene.rootNodes.push_back(static_cast<uint32_t>(r));
    }
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// GltfLoader::Load
// ─────────────────────────────────────────────────────────────────────────────
std::optional<SceneData> GltfLoader::Load(const std::string& path) {
    tinygltf::TinyGLTF loader;
    tinygltf::Model    model;
    std::string        err, warn;

    // Provide a custom image loader that delegates to our ImageLoader.
    loader.SetImageLoader(
        [](tinygltf::Image* image, const int /*imageIdx*/,
           std::string* err, std::string* /*warn*/,
           int /*reqWidth*/, int /*reqHeight*/,
           const unsigned char* bytes, int size, void* /*userData*/) -> bool {
            auto result = ImageLoader::LoadFromMemory(
                bytes, static_cast<size_t>(size), image->name);
            if (!result) {
                if (err) *err = "stb_image failed on embedded image: " + image->name;
                return false;
            }
            image->width    = static_cast<int>(result->width);
            image->height   = static_cast<int>(result->height);
            image->component = 4;
            image->bits      = 8;
            image->pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
            image->image.assign(result->pixels.begin(), result->pixels.end());
            return true;
        },
        nullptr);

    // Detect GLB vs GLTF by extension
    bool ok = false;
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".glb")
        ok = loader.LoadBinaryFromFile(&model, &err, &warn, path);
    else
        ok = loader.LoadASCIIFromFile(&model, &err, &warn, path);

    if (!warn.empty()) SA_LOG_WARN("GltfLoader '{}': {}", path, warn);
    if (!ok) {
        SA_LOG_ERROR("GltfLoader: failed to load '{}' — {}", path, err);
        return std::nullopt;
    }

    SceneData scene;
    scene.sourcePath = path;

    // ── Images ────────────────────────────────────────────────────────────────
    scene.images.reserve(model.images.size());
    for (size_t i = 0; i < model.images.size(); i++) {
        const auto& gi = model.images[i];
        ImageData img;
        img.path     = gi.uri.empty() ? gi.name : gi.uri;
        img.width    = static_cast<uint32_t>(gi.width);
        img.height   = static_cast<uint32_t>(gi.height);
        img.channels = static_cast<uint32_t>(gi.component);
        img.isHDR    = false;
        img.pixels.assign(gi.image.begin(), gi.image.end());
        scene.images.push_back(std::move(img));
    }

    // ── Materials ─────────────────────────────────────────────────────────────
    scene.materials.reserve(model.materials.size());
    for (const auto& gm : model.materials)
        scene.materials.push_back(ConvertMaterial(gm, model));

    // ── Meshes ────────────────────────────────────────────────────────────────
    scene.meshes.reserve(model.meshes.size());
    for (const auto& gm : model.meshes) {
        MeshData mesh;
        mesh.name = gm.name;
        mesh.primitives.reserve(gm.primitives.size());
        for (const auto& gp : gm.primitives)
            mesh.primitives.push_back(ConvertPrimitive(model, gp));
        scene.meshes.push_back(std::move(mesh));
    }

    // ── Nodes + hierarchy ────────────────────────────────────────────────────
    ConvertNodes(model, scene);

    SA_LOG_INFO("GltfLoader: loaded '{}' — {} mesh(es), {} material(s), "
                "{} image(s), {} node(s), {} vertices, {} indices",
                path,
                scene.meshes.size(), scene.materials.size(),
                scene.images.size(), scene.nodes.size(),
                scene.TotalVertexCount(), scene.TotalIndexCount());

    return scene;
}

} // namespace StellarAlia::Resource
