#include "MeshCook.hpp"
#include "MaterialCook.hpp"

#include "resource/cook/CookedMesh.hpp"
#include "resource/cook/CookedTexture.hpp"
#include "resource/loaders/GltfLoader.hpp"
#include "resource/types/MeshData.hpp"

#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>

namespace StellarAlia::Cook {

using namespace StellarAlia::Resource;

// ─── helpers ─────────────────────────────────────────────────────────────────

// Derive a deterministic child AssetID from a parent mesh UUID and image index.
// Uses Fibonacci/golden-ratio hashing constants so IDs spread uniformly even
// for small image indices and won't collide with randomly-generated UUIDs.
static AssetID DeriveImageID(const AssetID& meshId, int32_t imageIndex) {
    const uint64_t idx = static_cast<uint64_t>(imageIndex) + 1u;
    AssetID id;
    id.hi = meshId.hi ^ (idx * 0x9e3779b97f4a7c15ULL);
    id.lo = meshId.lo ^ (idx * 0x6c62272e07bb0142ULL);
    // Stamp as UUID v4 (version bits 76-79 = 0100) + variant (bits 64-65 = 10)
    id.hi = (id.hi & 0xFFFFFFFFFFFF0FFFull) | 0x0000000000004000ull;
    id.lo = (id.lo & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;
    return id;
}

static bool NeedsRecook(const AssetEntry& entry, const fs::path& outPath) {
    if (!fs::exists(outPath)) return true;
    // Force re-cook if the .samesh was produced by an older format version.
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

// ─── CookMesh ────────────────────────────────────────────────────────────────

bool CookMesh(const AssetEntry& entry, const fs::path& outputDir, bool force) {
    fs::create_directories(outputDir);

    const fs::path outPath = outputDir / (entry.meta.uuid.ToString() + ".samesh");

    if (!force && !NeedsRecook(entry, outPath)) {
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
    // Each image gets a deterministic AssetID derived from the mesh UUID + index.
    // baseColor and emissive textures are sRGB; all others are linear.
    for (int32_t imgIdx = 0; imgIdx < static_cast<int32_t>(scene.images.size()); ++imgIdx) {
        const ImageData& img = scene.images[imgIdx];
        if (img.pixels.empty()) continue;

        const AssetID texId = DeriveImageID(entry.meta.uuid, imgIdx);
        const fs::path texOut = outputDir / (texId.ToString() + ".satex");
        if (fs::exists(texOut)) continue;   // already cooked (incremental)

        // Determine sRGB: baseColor and emissive use sRGB; normal/metal/occlusion are linear.
        // Check if any material's baseColor or emissive points to this image.
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
    // Build a map: glTF material index → cooked AssetID.
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

    // Merge all primitives into one VB / IB.
    // Traverse node hierarchy (DFS) to bake per-node world transforms into each
    // submesh.  Falls back to flat mesh iteration if no root nodes are present.
    CookedMesh cooked;
    cooked.id           = entry.meta.uuid;
    cooked.vertexStride = sizeof(Vertex);   // 48 bytes
    cooked.indexStride  = sizeof(uint32_t); // 4  bytes

    uint32_t globalVertexOffset = 0;
    uint32_t globalIndexOffset  = 0;

    // Lambda that appends one primitive as a CookedSubMesh.
    auto appendPrimitive = [&](const Primitive& prim, const glm::mat4& worldTf) {
        if (prim.vertices.empty()) return;

        CookedSubMesh sm;
        sm.vertexOffset   = globalVertexOffset;
        sm.vertexCount    = static_cast<uint32_t>(prim.vertices.size());
        sm.indexOffset    = globalIndexOffset;
        sm.indexCount     = static_cast<uint32_t>(prim.indices.size());
        sm.materialIndex  = prim.materialIndex;
        sm.localTransform = worldTf;
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

        globalVertexOffset += sm.vertexCount;
        globalIndexOffset  += sm.indexCount;
    };

    if (!scene.rootNodes.empty()) {
        // DFS: accumulate world transform per node.
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
        // Fallback: flat iteration, identity transform per primitive.
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
              << " submeshes=" << cooked.subMeshes.size() << ")\n";
    return true;
}

} // namespace StellarAlia::Cook
