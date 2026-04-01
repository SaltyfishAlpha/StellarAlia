#include "MeshCook.hpp"

#include "resource/cook/CookedMesh.hpp"
#include "resource/loaders/GltfLoader.hpp"
#include "resource/types/MeshData.hpp"

#include <cstring>
#include <iostream>

namespace StellarAlia::Cook {

using namespace StellarAlia::Resource;

// ─── helpers ─────────────────────────────────────────────────────────────────

static bool NeedsRecook(const AssetEntry& entry, const fs::path& outPath) {
    if (!fs::exists(outPath)) return true;
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

    // Merge all primitives from all meshes into one VB / IB.
    // Each primitive becomes a CookedSubMesh.
    CookedMesh cooked;
    cooked.id           = entry.meta.uuid;
    cooked.vertexStride = sizeof(Vertex);   // 48 bytes
    cooked.indexStride  = sizeof(uint32_t); // 4  bytes

    uint32_t globalVertexOffset = 0;
    uint32_t globalIndexOffset  = 0;

    for (const auto& mesh : scene.meshes) {
        for (const auto& prim : mesh.primitives) {
            if (prim.vertices.empty()) continue;

            CookedSubMesh sm;
            sm.vertexOffset  = globalVertexOffset;
            sm.vertexCount   = static_cast<uint32_t>(prim.vertices.size());
            sm.indexOffset   = globalIndexOffset;
            sm.indexCount    = static_cast<uint32_t>(prim.indices.size());
            sm.materialIndex = prim.materialIndex;
            cooked.subMeshes.push_back(sm);

            // Append vertex data (raw copy of Vertex structs).
            const size_t vbBytes = prim.vertices.size() * sizeof(Vertex);
            const size_t prevVbSize = cooked.vertexData.size();
            cooked.vertexData.resize(prevVbSize + vbBytes);
            memcpy(cooked.vertexData.data() + prevVbSize,
                        prim.vertices.data(), vbBytes);

            // Append index data.
            const size_t ibBytes = prim.indices.size() * sizeof(uint32_t);
            const size_t prevIbSize = cooked.indexData.size();
            cooked.indexData.resize(prevIbSize + ibBytes);
            memcpy(cooked.indexData.data() + prevIbSize,
                        prim.indices.data(), ibBytes);

            globalVertexOffset += sm.vertexCount;
            globalIndexOffset  += sm.indexCount;
        }
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
