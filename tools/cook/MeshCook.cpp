#include "MeshCook.hpp"
#include "MaterialCook.hpp"

#include "resource/cook/CookedMesh.hpp"
#include "resource/cook/CookedSkeleton.hpp"
#include "resource/cook/CookedAnim.hpp"
#include "resource/cook/CookedTexture.hpp"
#include "resource/loaders/GltfLoader.hpp"
#include "resource/types/MeshData.hpp"

#include <nlohmann/json.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>

namespace StellarAlia::Cook {

using namespace StellarAlia::Resource;

// ─── helpers ─────────────────────────────────────────────────────────────────

static AssetID DeriveImageID(const AssetID& meshId, int32_t imageIndex) {
    const uint64_t idx = static_cast<uint64_t>(imageIndex) + 1u;
    AssetID id;
    id.hi = meshId.hi ^ (idx * 0x9e3779b97f4a7c15ULL);
    id.lo = meshId.lo ^ (idx * 0x6c62272e07bb0142ULL);
    id.hi = (id.hi & 0xFFFFFFFFFFFF0FFFull) | 0x0000000000004000ull;
    id.lo = (id.lo & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;
    return id;
}

static bool NeedsRecook(const AssetEntry& entry, const fs::path& outPath) {
    if (!fs::exists(outPath)) return true;
    if (outPath.extension() == ".samesh") {
        std::ifstream vf(outPath, std::ios::binary);
        uint32_t magic = 0, version = 0;
        vf.read(reinterpret_cast<char*>(&magic),   sizeof(magic));
        vf.read(reinterpret_cast<char*>(&version), sizeof(version));
        namespace SF = StellarAlia::Resource::SameshFormat;
        if (!vf || magic != SF::Magic || version != SF::Version)
            return true;
    }
    const auto outTime = fs::last_write_time(outPath);
    if (fs::last_write_time(entry.sourcePath) > outTime) return true;
    if (fs::exists(entry.metaPath) && fs::last_write_time(entry.metaPath) > outTime) return true;
    return false;
}

// ─── Per-node split ───────────────────────────────────────────────────────────
//
// For static (non-skinned) glTF files, cook a separate .samesh per node that
// carries geometry (identity localTransform — entity TransformComponent owns
// the node's local pose).  Also writes a .sanode JSON manifest that maps node
// indices to mesh UUIDs and records the scene hierarchy for the import UI.

using json = nlohmann::json;

static void CookPerNodeMeshes(
    const AssetEntry&           entry,
    const SceneData&            scene,
    const fs::path&             outputDir,
    const std::vector<AssetID>& matIDs,
    bool                        force)
{
    if (scene.nodes.empty()) return;

    // Build parent index table.
    std::vector<int32_t> parentOf(scene.nodes.size(), -1);
    for (uint32_t ni = 0; ni < static_cast<uint32_t>(scene.nodes.size()); ++ni)
        for (uint32_t ci : scene.nodes[ni].children)
            if (ci < static_cast<uint32_t>(scene.nodes.size()))
                parentOf[ci] = static_cast<int32_t>(ni);

    json manifest;
    manifest["version"]      = 1;
    manifest["file_mesh_id"] = entry.meta.uuid.ToString();
    manifest["nodes"]        = json::array();

    for (uint32_t ni = 0; ni < static_cast<uint32_t>(scene.nodes.size()); ++ni) {
        const SceneNode& sn = scene.nodes[ni];

        json nj;
        nj["name"]       = sn.name;
        nj["node_idx"]   = ni;
        nj["parent_idx"] = parentOf[ni];

        // Store local transform as 16-element column-major float array.
        const float* tfPtr = glm::value_ptr(sn.localTransform);
        for (int i = 0; i < 16; ++i)
            nj["local_transform"].push_back(tfPtr[i]);

        if (sn.meshIndex >= 0 &&
            sn.meshIndex < static_cast<int32_t>(scene.meshes.size()))
        {
            const AssetID  nodeId  = DeriveNodeMeshID(entry.meta.uuid, ni);
            const fs::path outPath = outputDir / (nodeId.ToString() + ".samesh");
            nj["mesh_id"] = nodeId.ToString();

            if (force || !fs::exists(outPath)) {
                CookedMesh cooked;
                cooked.id           = nodeId;
                cooked.vertexStride = sizeof(Vertex);
                cooked.indexStride  = sizeof(uint32_t);

                uint32_t vOff = 0, iOff = 0;
                for (const auto& prim : scene.meshes[sn.meshIndex].primitives) {
                    if (prim.vertices.empty()) continue;

                    CookedSubMesh sm;
                    sm.vertexOffset   = vOff;
                    sm.vertexCount    = static_cast<uint32_t>(prim.vertices.size());
                    sm.indexOffset    = iOff;
                    sm.indexCount     = static_cast<uint32_t>(prim.indices.size());
                    sm.materialIndex  = prim.materialIndex;
                    sm.localTransform = glm::mat4(1.f);  // identity — entity owns the pose
                    if (prim.materialIndex >= 0 &&
                        prim.materialIndex < static_cast<int32_t>(matIDs.size()))
                        sm.defaultMaterialID = matIDs[prim.materialIndex];
                    cooked.subMeshes.push_back(sm);

                    const size_t vbBytes    = prim.vertices.size() * sizeof(Vertex);
                    const size_t prevVbSize = cooked.vertexData.size();
                    cooked.vertexData.resize(prevVbSize + vbBytes);
                    std::memcpy(cooked.vertexData.data() + prevVbSize,
                                prim.vertices.data(), vbBytes);

                    const size_t ibBytes    = prim.indices.size() * sizeof(uint32_t);
                    const size_t prevIbSize = cooked.indexData.size();
                    cooked.indexData.resize(prevIbSize + ibBytes);
                    std::memcpy(cooked.indexData.data() + prevIbSize,
                                prim.indices.data(), ibBytes);

                    vOff += sm.vertexCount;
                    iOff += sm.indexCount;
                }
                cooked.vertexCount = vOff;
                cooked.indexCount  = iOff;

                if (!cooked.subMeshes.empty()) {
                    if (SaveCookedMesh(cooked, outPath.string()))
                        std::cout << "[Cook] NODE  node #" << ni
                                  << " '" << sn.name << "'"
                                  << "  →  " << outPath.filename() << '\n';
                    else
                        std::cerr << "[Cook] WARN  failed to write per-node mesh for node #"
                                  << ni << '\n';
                }
            }
        }

        manifest["nodes"].push_back(std::move(nj));
    }

    // Write manifest (.sanode).
    const fs::path manifestPath =
        outputDir / (entry.meta.uuid.ToString() + ".sanode");
    if (force || !fs::exists(manifestPath)) {
        std::ofstream mf(manifestPath);
        if (mf) {
            mf << manifest.dump(2);
            std::cout << "[Cook] NODES manifest  "
                      << entry.sourcePath.filename()
                      << "  →  " << manifestPath.filename() << '\n';
        } else {
            std::cerr << "[Cook] WARN  failed to write .sanode manifest\n";
        }
    }
}

// ─── CookMesh ────────────────────────────────────────────────────────────────

bool CookMesh(const AssetEntry& entry, const fs::path& outputDir, bool force) {
    fs::create_directories(outputDir);

    const fs::path outPath    = outputDir / (entry.meta.uuid.ToString() + ".samesh");
    const fs::path sanodePath = outputDir / (entry.meta.uuid.ToString() + ".sanode");

    // Skip only when the monolithic mesh is cached AND the .sanode manifest exists.
    // The manifest is written by CookPerNodeMeshes for static models; its absence means
    // either the model is being cooked for the first time with per-node support, or the
    // per-node outputs were purged and need regenerating.
    if (!force && !NeedsRecook(entry, outPath) && fs::exists(sanodePath)) {
        std::cout << "[Cook] SKIP (up-to-date)  " << entry.sourcePath.filename() << '\n';
        return true;
    }

    auto sceneOpt = GltfLoader::Load(entry.sourcePath.string());
    if (!sceneOpt) {
        std::cerr << "[Cook] FAIL  " << entry.sourcePath.filename()
                  << " — could not load glTF\n";
        return false;
    }

    const SceneData& scene = *sceneOpt;

    // ── Cook embedded images → .satex ─────────────────────────────────────────
    for (int32_t imgIdx = 0; imgIdx < static_cast<int32_t>(scene.images.size()); ++imgIdx) {
        const ImageData& img = scene.images[imgIdx];
        if (img.pixels.empty()) continue;

        const AssetID texId = DeriveImageID(entry.meta.uuid, imgIdx);
        const fs::path texOut = outputDir / (texId.ToString() + ".satex");
        if (fs::exists(texOut)) continue;

        bool isSrgb = false;
        for (const auto& mat : scene.materials) {
            if (mat.baseColorTexture.imageIndex == imgIdx ||
                mat.emissiveTexture.imageIndex  == imgIdx) {
                isSrgb = true;
                break;
            }
        }

        CookedTexture cooked;
        cooked.id        = texId;
        cooked.width     = img.width;
        cooked.height    = img.height;
        cooked.mipLevels = 1;
        cooked.srgb      = isSrgb;
        cooked.isHDR     = false;
        cooked.format    = CookedTextureFormat::RGBA8;
        CookedTextureMip mip0;
        mip0.offset = 0;
        mip0.size   = img.pixels.size();
        cooked.mips.push_back(mip0);
        cooked.data = img.pixels;

        if (SaveCookedTexture(cooked, texOut.string()))
            std::cout << "[Cook] TEX   embedded image #" << imgIdx
                      << " (" << img.width << 'x' << img.height
                      << ' ' << (isSrgb ? "sRGB" : "linear")
                      << ")  →  " << texOut.filename() << '\n';
        else
            std::cerr << "[Cook] WARN  failed to write embedded image #" << imgIdx << '\n';
    }

    // ── Cook each glTF material → companion .samat file ───────────────────────
    auto resolveImageID = [&](int32_t imgIdx) -> AssetID {
        if (imgIdx < 0 || imgIdx >= static_cast<int32_t>(scene.images.size()))
            return AssetID::Invalid();
        return DeriveImageID(entry.meta.uuid, imgIdx);
    };

    std::vector<AssetID> matIDs(scene.materials.size());
    for (int32_t mi = 0; mi < static_cast<int32_t>(scene.materials.size()); ++mi) {
        matIDs[mi] = Cook::DeriveMaterialID(entry.meta.uuid, mi);
        Cook::CookMaterial(scene.materials[mi], matIDs[mi], resolveImageID, outputDir);
    }

    // ── Cook skeletons → .saskel ──────────────────────────────────────────────
    for (int32_t si = 0; si < static_cast<int32_t>(scene.skins.size()); ++si) {
        const SkeletonData& skin = scene.skins[si];
        const AssetID skelId = DeriveSkinID(entry.meta.uuid, si);
        const fs::path skelOut = outputDir / (skelId.ToString() + ".saskel");

        CookedSkeleton skel;
        skel.id    = skelId;
        skel.bones = skin.bones;

        if (SaveCookedSkeleton(skel, skelOut.string()))
            std::cout << "[Cook] SKEL  skin #" << si
                      << " '" << skin.name << "'"
                      << " (" << skin.bones.size() << " bones)"
                      << "  →  " << skelOut.filename() << '\n';
        else
            std::cerr << "[Cook] WARN  failed to write skeleton #" << si << '\n';
    }

    // ── Cook animations → .saanim ─────────────────────────────────────────────
    for (int32_t ai = 0; ai < static_cast<int32_t>(scene.animations.size()); ++ai) {
        const AnimClip& clip = scene.animations[ai];
        const AssetID animId = DeriveAnimID(entry.meta.uuid, ai);
        const fs::path animOut = outputDir / (animId.ToString() + ".saanim");

        CookedAnim cooked;
        cooked.id   = animId;
        cooked.clip = clip;

        if (SaveCookedAnim(cooked, animOut.string()))
            std::cout << "[Cook] ANIM  anim #" << ai
                      << " '" << clip.name << "'"
                      << " (" << clip.channels.size() << " channels, "
                      << clip.duration << "s)"
                      << "  →  " << animOut.filename() << '\n';
        else
            std::cerr << "[Cook] WARN  failed to write animation #" << ai << '\n';
    }

    // ── Merge primitives into one VB / IB (+ optional skin data) ──────────────
    CookedMesh cooked;
    cooked.id           = entry.meta.uuid;
    cooked.vertexStride = sizeof(Vertex);   // 48 bytes
    cooked.indexStride  = sizeof(uint32_t); // 4  bytes

    uint32_t globalVertexOffset = 0;
    uint32_t globalIndexOffset  = 0;
    bool     anySkinned         = false;

    // Determine whether any primitive is skinned (has skin data).
    for (const auto& mesh : scene.meshes)
        for (const auto& prim : mesh.primitives)
            if (!prim.skinVertices.empty()) { anySkinned = true; break; }

    auto appendPrimitive = [&](const Primitive& prim, const glm::mat4& worldTf) {
        if (prim.vertices.empty()) return;

        // Skinned primitives: use identity local transform — skeleton drives poses.
        const glm::mat4 localTf = prim.skinVertices.empty() ? worldTf : glm::mat4(1.f);

        CookedSubMesh sm;
        sm.vertexOffset   = globalVertexOffset;
        sm.vertexCount    = static_cast<uint32_t>(prim.vertices.size());
        sm.indexOffset    = globalIndexOffset;
        sm.indexCount     = static_cast<uint32_t>(prim.indices.size());
        sm.materialIndex  = prim.materialIndex;
        sm.localTransform = localTf;
        if (prim.materialIndex >= 0 &&
            prim.materialIndex < static_cast<int32_t>(matIDs.size()))
            sm.defaultMaterialID = matIDs[prim.materialIndex];
        cooked.subMeshes.push_back(sm);

        const size_t vbBytes    = prim.vertices.size() * sizeof(Vertex);
        const size_t prevVbSize = cooked.vertexData.size();
        cooked.vertexData.resize(prevVbSize + vbBytes);
        memcpy(cooked.vertexData.data() + prevVbSize, prim.vertices.data(), vbBytes);

        const size_t ibBytes    = prim.indices.size() * sizeof(uint32_t);
        const size_t prevIbSize = cooked.indexData.size();
        cooked.indexData.resize(prevIbSize + ibBytes);
        memcpy(cooked.indexData.data() + prevIbSize, prim.indices.data(), ibBytes);

        // Skin data: must be dense and match vertex count.
        if (anySkinned) {
            const size_t svBytes    = prim.vertices.size() * sizeof(SkinVertex);
            const size_t prevSvSize = cooked.skinData.size();
            cooked.skinData.resize(prevSvSize + svBytes);
            if (!prim.skinVertices.empty() &&
                prim.skinVertices.size() == prim.vertices.size()) {
                memcpy(cooked.skinData.data() + prevSvSize,
                       prim.skinVertices.data(), svBytes);
            } else {
                // Unskinned primitive in a skinned mesh: fill with identity weights.
                SkinVertex identity{};
                for (size_t vi = 0; vi < prim.vertices.size(); ++vi) {
                    memcpy(cooked.skinData.data() + prevSvSize + vi * sizeof(SkinVertex),
                           &identity, sizeof(SkinVertex));
                }
            }
        }

        globalVertexOffset += sm.vertexCount;
        globalIndexOffset  += sm.indexCount;
    };

    if (!scene.rootNodes.empty()) {
        std::function<void(uint32_t, const glm::mat4&)> dfs;
        dfs = [&](uint32_t nodeIdx, const glm::mat4& parentTf) {
            if (nodeIdx >= scene.nodes.size()) return;
            const SceneNode& node   = scene.nodes[nodeIdx];
            const glm::mat4  worldTf = parentTf * node.localTransform;

            if (node.meshIndex >= 0 &&
                node.meshIndex < static_cast<int32_t>(scene.meshes.size()))
            {
                for (const auto& prim : scene.meshes[node.meshIndex].primitives)
                    appendPrimitive(prim, worldTf);
            }

            for (uint32_t child : node.children)
                dfs(child, worldTf);
        };
        for (uint32_t root : scene.rootNodes)
            dfs(root, glm::mat4(1.0f));
    } else {
        for (const auto& mesh : scene.meshes)
            for (const auto& prim : mesh.primitives)
                appendPrimitive(prim, glm::mat4(1.0f));
    }

    if (cooked.subMeshes.empty()) {
        std::cerr << "[Cook] FAIL  " << entry.sourcePath.filename()
                  << " — no valid primitives found\n";
        return false;
    }

    cooked.vertexCount = globalVertexOffset;
    cooked.indexCount  = globalIndexOffset;

    if (!SaveCookedMesh(cooked, outPath.string())) {
        std::cerr << "[Cook] FAIL  " << entry.sourcePath.filename()
                  << " — could not write .samesh\n";
        return false;
    }

    std::cout << "[Cook] OK    " << entry.sourcePath.filename()
              << "  →  " << outPath.filename()
              << "  (vertices=" << cooked.vertexCount
              << " indices=" << cooked.indexCount
              << " submeshes=" << cooked.subMeshes.size()
              << (anySkinned ? " SKINNED" : "")
              << ")\n";

    // Per-node split: only for static (non-skinned) glTF with a node hierarchy.
    // Skinned models write a minimal sentinel .sanode so the up-to-date check
    // (fs::exists(sanodePath)) works correctly on subsequent cook invocations.
    if (!anySkinned) {
        CookPerNodeMeshes(entry, scene, outputDir, matIDs, force);
    } else {
        if (force || !fs::exists(sanodePath)) {
            json sentinel;
            sentinel["version"]      = 1;
            sentinel["file_mesh_id"] = entry.meta.uuid.ToString();
            sentinel["skinned"]      = true;
            sentinel["nodes"]        = json::array();
            std::ofstream sf(sanodePath);
            if (sf) sf << sentinel.dump(2);
        }
    }

    return true;
}

} // namespace StellarAlia::Cook
