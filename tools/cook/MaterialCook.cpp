#include "MaterialCook.hpp"
#include "core/logs/Log.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>

namespace StellarAlia::Cook {

using json = nlohmann::json;

// ─── DeriveMaterialID ────────────────────────────────────────────────────────

AssetID DeriveMaterialID(const AssetID& meshId, int32_t matIndex) {
    // Different Fibonacci constants from DeriveImageID to prevent UUID collisions.
    const uint64_t idx = static_cast<uint64_t>(matIndex) + 1u;
    AssetID id;
    id.hi = meshId.hi ^ (idx * 0xd37e9a1ce2148403ULL);
    id.lo = meshId.lo ^ (idx * 0x85157af0f7e14b2dULL);
    // Stamp as UUID v4 + variant bits.
    id.hi = (id.hi & 0xFFFFFFFFFFFF0FFFull) | 0x0000000000004000ull;
    id.lo = (id.lo & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;
    return id;
}

// ─── CookMaterial ─────────────────────────────────────────────────────────────

bool CookMaterial(const Resource::MaterialData& mat,
                  const AssetID& matID,
                  const std::function<AssetID(int32_t)>& resolveTexID,
                  const std::filesystem::path& outputDir) {
    const std::filesystem::path outPath = outputDir / (matID.ToString() + ".samat");
    if (std::filesystem::exists(outPath)) return true;   // incremental skip

    // Helper: imageIndex → UUID string, or empty string if not present.
    auto texUUID = [&](int32_t imgIdx) -> std::string {
        if (imgIdx < 0) return "";
        const AssetID id = resolveTexID(imgIdx);
        return id.IsValid() ? id.ToString() : "";
    };

    json root;
    root["version"] = 1;
    root["type"]    = "PBR";

    root["params"]["baseColorFactor"]   = {mat.baseColorFactor.x, mat.baseColorFactor.y,
                                           mat.baseColorFactor.z, mat.baseColorFactor.w};
    root["params"]["roughnessFactor"]   = mat.roughnessFactor;
    root["params"]["metallicFactor"]    = mat.metallicFactor;
    root["params"]["normalScale"]       = mat.normalScale;
    root["params"]["occlusionStrength"] = mat.occlusionStrength;
    root["params"]["emissiveFactor"]    = {mat.emissiveFactor.x,
                                           mat.emissiveFactor.y,
                                           mat.emissiveFactor.z};

    root["textures"]["t_BaseColor"]         = texUUID(mat.baseColorTexture.imageIndex);
    root["textures"]["t_Normal"]            = texUUID(mat.normalTexture.imageIndex);
    root["textures"]["t_MetallicRoughness"] = texUUID(mat.metallicRoughnessTexture.imageIndex);
    root["textures"]["t_Occlusion"]         = texUUID(mat.occlusionTexture.imageIndex);
    root["textures"]["t_Emissive"]          = texUUID(mat.emissiveTexture.imageIndex);

    std::ofstream f(outPath);
    if (!f) {
        std::cerr << "[Cook] WARN  failed to write " << outPath.filename() << '\n';
        return false;
    }
    f << root.dump(2);
    std::cout << "[Cook] MAT   " << (mat.name.empty() ? "(unnamed)" : mat.name)
              << "  →  " << outPath.filename() << '\n';
    return f.good();
}

} // namespace StellarAlia::Cook
