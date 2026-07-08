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
#include "resource/loaders/MeshUtils.hpp"
#include "core/logs/Log.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cstring>
#include <fstream>

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
                            const tinygltf::Primitive& prim,
                            int nodeSkinIndex) {
    Primitive out;
    out.materialIndex = prim.material;
    out.skinIndex     = nodeSkinIndex;

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

    // ── Skinning: JOINTS_0 + WEIGHTS_0 ───────────────────────────────────────
    auto jointsIt  = prim.attributes.find("JOINTS_0");
    auto weightsIt = prim.attributes.find("WEIGHTS_0");
    if (jointsIt != prim.attributes.end() && weightsIt != prim.attributes.end()) {
        out.skinVertices.resize(vertCount);

        // JOINTS_0: UBYTE4 or USHORT4
        {
            const auto& acc = model.accessors[jointsIt->second];
            const auto& bv  = model.bufferViews[acc.bufferView];
            const auto& buf = model.buffers[bv.buffer];
            const uint8_t* raw = buf.data.data() + bv.byteOffset + acc.byteOffset;

            for (size_t i = 0; i < vertCount; i++) {
                if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                    const uint8_t* j = raw + i * 4;
                    out.skinVertices[i].joints = {j[0], j[1], j[2], j[3]};
                } else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                    const uint16_t* j = reinterpret_cast<const uint16_t*>(raw) + i * 4;
                    out.skinVertices[i].joints = {j[0], j[1], j[2], j[3]};
                }
            }
        }

        // WEIGHTS_0: FLOAT4 or UBYTE4 or USHORT4
        {
            const auto& acc = model.accessors[weightsIt->second];
            const auto& bv  = model.bufferViews[acc.bufferView];
            const auto& buf = model.buffers[bv.buffer];
            const uint8_t* raw = buf.data.data() + bv.byteOffset + acc.byteOffset;

            for (size_t i = 0; i < vertCount; i++) {
                if (acc.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
                    const float* w = reinterpret_cast<const float*>(raw) + i * 4;
                    out.skinVertices[i].weights = {w[0], w[1], w[2], w[3]};
                } else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                    const uint16_t* w = reinterpret_cast<const uint16_t*>(raw) + i * 4;
                    out.skinVertices[i].weights = {
                        w[0]/65535.f, w[1]/65535.f, w[2]/65535.f, w[3]/65535.f};
                } else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                    const uint8_t* w = raw + i * 4;
                    out.skinVertices[i].weights = {
                        w[0]/255.f, w[1]/255.f, w[2]/255.f, w[3]/255.f};
                }
            }
        }
    }

    // ── Fallback tangents (Issue #108) ────────────────────────────────────────
    // Runs last: MikkTSpace re-welds vertices, so every parallel attribute
    // (including skinVertices) must already be populated.
    if (tanIt == prim.attributes.end() && uvIt != prim.attributes.end())
        MeshUtils::GenerateTangents(out.vertices, out.indices,
                                    out.skinVertices.empty() ? nullptr
                                                             : &out.skinVertices);

    return out;
}

// ── Material conversion ───────────────────────────────────────────────────────
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

    {
        auto extIt = mat.extensions.find("KHR_materials_emissive_strength");
        if (extIt != mat.extensions.end() && extIt->second.IsObject()) {
            const tinygltf::Value& sv = extIt->second.Get("emissiveStrength");
            float strength = sv.IsReal() ? static_cast<float>(sv.Get<double>()) :
                             sv.IsInt()  ? static_cast<float>(sv.Get<int>())    : 1.0f;
            out.emissiveFactor *= strength;
        }
    }

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
        sn.skinIndex       = gn.skin;
        sn.children.reserve(gn.children.size());
        for (int c : gn.children)
            sn.children.push_back(static_cast<uint32_t>(c));
    }

    int sceneIdx = model.defaultScene >= 0 ? model.defaultScene : 0;
    if (!model.scenes.empty()) {
        for (int r : model.scenes[sceneIdx].nodes)
            scene.rootNodes.push_back(static_cast<uint32_t>(r));
    }
}

// ── Skeleton (skin) extraction ────────────────────────────────────────────────
void ConvertSkins(const tinygltf::Model& model, SceneData& scene) {
    scene.skins.reserve(model.skins.size());
    for (const auto& gs : model.skins) {
        SkeletonData skel;
        skel.name = gs.name;
        skel.bones.resize(gs.joints.size());

        // Build parent index map: nodeIndex → jointIndex within this skin.
        std::unordered_map<int, int32_t> nodeToJoint;
        for (int32_t ji = 0; ji < (int32_t)gs.joints.size(); ++ji)
            nodeToJoint[gs.joints[ji]] = ji;

        // Inverse bind matrices (column-major, one per joint).
        const glm::mat4* ibm = nullptr;
        if (gs.inverseBindMatrices >= 0)
            ibm = AccessorData<glm::mat4>(model, gs.inverseBindMatrices);

        for (int32_t ji = 0; ji < (int32_t)gs.joints.size(); ++ji) {
            int nodeIdx = gs.joints[ji];
            skel.bones[ji].name = model.nodes[nodeIdx].name;
            skel.bones[ji].inverseBindMatrix = ibm ? ibm[ji] : glm::mat4(1.f);

            // Find parent: the glTF node's parent within the joint set.
            skel.bones[ji].parentIndex = -1;
            for (int32_t pji = 0; pji < (int32_t)gs.joints.size(); ++pji) {
                const auto& parentNode = model.nodes[gs.joints[pji]];
                for (int child : parentNode.children) {
                    if (child == nodeIdx) {
                        skel.bones[ji].parentIndex = pji;
                        break;
                    }
                }
                if (skel.bones[ji].parentIndex >= 0) break;
            }
        }

        scene.skins.push_back(std::move(skel));
    }
}

// ── Animation extraction ──────────────────────────────────────────────────────
void ConvertAnimations(const tinygltf::Model& model, SceneData& scene,
                       const std::vector<std::vector<int>>& skinJoints) {
    scene.animations.reserve(model.animations.size());
    for (const auto& ga : model.animations) {
        AnimClip clip;
        clip.name = ga.name;

        for (const auto& gch : ga.channels) {
            if (gch.sampler < 0 || gch.sampler >= (int)ga.samplers.size()) continue;
            if (gch.target_node < 0) continue;

            const auto& sampler = ga.samplers[gch.sampler];

            // Only Translation, Rotation, Scale are supported.
            AnimChannel::Target target;
            if (gch.target_path == "translation")      target = AnimChannel::Target::Translation;
            else if (gch.target_path == "rotation")    target = AnimChannel::Target::Rotation;
            else if (gch.target_path == "scale")       target = AnimChannel::Target::Scale;
            else continue;

            AnimChannel::Interp interp = AnimChannel::Interp::Linear;
            if (sampler.interpolation == "STEP")         interp = AnimChannel::Interp::Step;
            // CUBICSPLINE is downgraded to LINEAR.

            // Map target_node to a bone index within any skin.
            // Use the first skin that contains this node.
            int32_t boneIndex = -1;
            for (const auto& joints : skinJoints) {
                for (int32_t ji = 0; ji < (int32_t)joints.size(); ++ji) {
                    if (joints[ji] == gch.target_node) {
                        boneIndex = ji;
                        break;
                    }
                }
                if (boneIndex >= 0) break;
            }
            if (boneIndex < 0) continue;  // target_node not in any skin joint list

            // Times (input accessor).
            const size_t kfCount = AccessorCount(model, sampler.input);
            if (kfCount == 0) continue;
            const float* times = AccessorData<float>(model, sampler.input);

            AnimChannel ch;
            ch.boneIndex = boneIndex;
            ch.target    = target;
            ch.interp    = interp;
            ch.times.assign(times, times + kfCount);

            // Values (output accessor) — always stored as vec4 for uniform layout.
            const auto& outAcc = model.accessors[sampler.output];
            const size_t valCount = (sampler.interpolation == "CUBICSPLINE")
                                    ? kfCount  // take only the middle values (in-tangent + value + out-tangent)
                                    : kfCount;
            ch.values.resize(valCount);

            if (target == AnimChannel::Target::Rotation) {
                // Rotation: xyzw quaternion stored directly.
                const glm::vec4* vals = AccessorData<glm::vec4>(model, sampler.output);
                const uint32_t stride = (sampler.interpolation == "CUBICSPLINE") ? 3 : 1;
                for (size_t ki = 0; ki < kfCount; ++ki)
                    ch.values[ki] = vals[ki * stride + (stride == 3 ? 1 : 0)];
            } else {
                // Translation/Scale: vec3 → stored as vec4 with w=0.
                const glm::vec3* vals = AccessorData<glm::vec3>(model, sampler.output);
                const uint32_t stride = (sampler.interpolation == "CUBICSPLINE") ? 3 : 1;
                for (size_t ki = 0; ki < kfCount; ++ki) {
                    const glm::vec3& v = vals[ki * stride + (stride == 3 ? 1 : 0)];
                    ch.values[ki] = {v.x, v.y, v.z, 0.f};
                }
            }

            if (!ch.times.empty())
                clip.duration = std::max(clip.duration, ch.times.back());

            clip.channels.push_back(std::move(ch));
        }

        scene.animations.push_back(std::move(clip));
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

    // Binary vs ASCII by content, not extension — .vrm (and any other glb
    // container) starts with the "glTF" magic; .gltf is bare JSON.
    bool isBinary = false;
    {
        std::ifstream probe(path, std::ios::binary);
        char magic[4] = {};
        probe.read(magic, 4);
        isBinary = probe && std::memcmp(magic, "glTF", 4) == 0;
    }

    bool ok = false;
    if (isBinary)
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

    // ── Skins (skeletons) ─────────────────────────────────────────────────────
    ConvertSkins(model, scene);

    // ── Nodes + hierarchy (stores skinIndex per node) ─────────────────────────
    ConvertNodes(model, scene);

    // ── Meshes (passes skinIndex from the node that references each primitive) ─
    // Build a per-node skinIndex lookup since gltf primitives don't carry skin info.
    // skin info comes from the node, not the primitive.
    std::vector<int> meshNodeSkin(model.meshes.size(), -1);
    for (const auto& gn : model.nodes) {
        if (gn.mesh >= 0 && gn.skin >= 0)
            meshNodeSkin[gn.mesh] = gn.skin;
    }

    scene.meshes.reserve(model.meshes.size());
    for (size_t mi = 0; mi < model.meshes.size(); ++mi) {
        const auto& gm = model.meshes[mi];
        MeshData mesh;
        mesh.name = gm.name;
        mesh.primitives.reserve(gm.primitives.size());
        const int skinIdx = meshNodeSkin[mi];
        for (const auto& gp : gm.primitives)
            mesh.primitives.push_back(ConvertPrimitive(model, gp, skinIdx));
        scene.meshes.push_back(std::move(mesh));
    }

    // ── Multi-skin merge (Issue #108) ─────────────────────────────────────────
    // VRM (and some DCC exports) ship one skin per mesh section over the same
    // armature. The runtime drives ONE skin-matrix buffer per entity — derived
    // skeleton is always skin #0 — so multiple skins must collapse into one:
    // union the joints (keyed by source node, first IBM wins), remap every
    // primitive's joint indices, and retarget animations at the merged set.
    std::vector<std::vector<int>> skinJoints;
    if (model.skins.size() > 1) {
        std::vector<int>                     mergedNodes;  // merged joint → node idx
        std::unordered_map<int, uint32_t>    mergedOf;     // node idx → merged joint
        std::vector<glm::mat4>               mergedIbm;
        std::vector<std::vector<uint32_t>>   remap(model.skins.size());

        for (size_t s = 0; s < model.skins.size(); ++s) {
            const auto& gs = model.skins[s];
            const glm::mat4* ibm = gs.inverseBindMatrices >= 0
                ? AccessorData<glm::mat4>(model, gs.inverseBindMatrices) : nullptr;
            remap[s].resize(gs.joints.size());
            for (size_t j = 0; j < gs.joints.size(); ++j) {
                auto [it, inserted] = mergedOf.try_emplace(
                    gs.joints[j], static_cast<uint32_t>(mergedNodes.size()));
                if (inserted) {
                    mergedNodes.push_back(gs.joints[j]);
                    mergedIbm.push_back(ibm ? ibm[j] : glm::mat4(1.f));
                } else if (ibm) {
                    // #83 P1: first wins — flag bind poses that disagree.
                    const glm::mat4& seen = mergedIbm[it->second];
                    bool same = true;
                    for (int c = 0; c < 4 && same; ++c)
                        for (int r = 0; r < 4; ++r)
                            if (std::fabs(ibm[j][c][r] - seen[c][r]) > 1e-4f) {
                                same = false; break;
                            }
                    if (!same)
                        SA_LOG_WARN("GltfLoader: joint '{}' has conflicting "
                                    "inverse-bind matrices across skins — "
                                    "first wins",
                                    model.nodes[gs.joints[j]].name);
                }
                remap[s][j] = it->second;
            }
        }

        std::vector<int> parentOf(model.nodes.size(), -1);
        for (size_t n = 0; n < model.nodes.size(); ++n)
            for (int c : model.nodes[n].children)
                if (c >= 0 && c < static_cast<int>(model.nodes.size()))
                    parentOf[c] = static_cast<int>(n);

        SkeletonData merged;
        merged.name = "MergedSkin";
        merged.bones.resize(mergedNodes.size());
        for (size_t j = 0; j < mergedNodes.size(); ++j) {
            merged.bones[j].name              = model.nodes[mergedNodes[j]].name;
            merged.bones[j].inverseBindMatrix = mergedIbm[j];
            merged.bones[j].parentIndex       = -1;
            for (int p = parentOf[mergedNodes[j]]; p >= 0; p = parentOf[p]) {
                if (auto it = mergedOf.find(p); it != mergedOf.end()) {
                    merged.bones[j].parentIndex = static_cast<int32_t>(it->second);
                    break;
                }
            }
        }
        scene.skins.clear();
        scene.skins.push_back(std::move(merged));

        for (auto& mesh : scene.meshes)
            for (auto& prim : mesh.primitives) {
                if (prim.skinIndex < 0 ||
                    prim.skinIndex >= static_cast<int32_t>(remap.size()))
                    continue;
                const auto& table = remap[prim.skinIndex];
                for (auto& sv : prim.skinVertices)
                    for (int k = 0; k < 4; ++k)
                        if (sv.joints[k] < table.size())
                            sv.joints[k] = table[sv.joints[k]];
                prim.skinIndex = 0;
            }
        for (auto& sn : scene.nodes)
            if (sn.skinIndex >= 0) sn.skinIndex = 0;

        skinJoints.push_back(std::move(mergedNodes));
        SA_LOG_INFO("GltfLoader: merged {} skins into one skeleton ({} joints)",
                    model.skins.size(), scene.skins[0].bones.size());
    } else {
        skinJoints.reserve(model.skins.size());
        for (const auto& gs : model.skins)
            skinJoints.push_back(gs.joints);
    }

    // ── Animations ────────────────────────────────────────────────────────────
    ConvertAnimations(model, scene, skinJoints);

    SA_LOG_INFO("GltfLoader: loaded '{}' — {} mesh(es), {} material(s), "
                "{} image(s), {} node(s), {} skin(s), {} anim(s), {} vertices, {} indices",
                path,
                scene.meshes.size(), scene.materials.size(),
                scene.images.size(), scene.nodes.size(),
                scene.skins.size(), scene.animations.size(),
                scene.TotalVertexCount(), scene.TotalIndexCount());

    return scene;
}

} // namespace StellarAlia::Resource
