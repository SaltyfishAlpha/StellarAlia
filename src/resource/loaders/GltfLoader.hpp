#pragma once

#include <optional>
#include <string>

#include "resource/types/MeshData.hpp"

namespace StellarAlia::Resource {

// ─────────────────────────────────────────────────────────────────────────────
// GltfLoader  —  loads glTF 2.0 / GLB files into CPU-side SceneData.
//
// Supports:
//   - Binary (.glb) and JSON (.gltf) with external / embedded buffers
//   - PBR metallic-roughness materials
//   - Scene hierarchy (nodes + transforms)
//   - Embedded and external textures (loaded via ImageLoader)
//
// Does NOT support (deferred to Stage 4+):
//   - Skinning / morph targets
//   - Animations
//   - Sparse accessors
// ─────────────────────────────────────────────────────────────────────────────
class GltfLoader {
public:
    [[nodiscard]] static std::optional<SceneData> Load(const std::string& path);
};

} // namespace StellarAlia::Resource
