#pragma once

#include <optional>
#include <string>

#include "resource/types/MeshData.hpp"

namespace StellarAlia::Resource {

// ─────────────────────────────────────────────────────────────────────────────
// FbxLoader — FBX into SceneData via ufbx (Issue #108).
//
// ufbx load options normalize the pain points at parse time: units → meters,
// axes → right-handed Y-up (glTF convention), FBX geometry transforms baked
// into vertices, missing normals generated. Covered: node hierarchy,
// per-material primitives (faces triangulated), lambert/phong/PBR material
// approximation, embedded (decoded via stb) and external textures, skinning
// (top-4 weights, renormalized), animation stacks baked to 30 Hz TRS keys —
// one AnimClip per stack, mirroring glTF's multi-clip sidecar flow.
// Not covered: blend shapes (morphs are out engine-wide), FBX-authored
// tangents (always regenerated via MikkTSpace for a consistent basis).
// ─────────────────────────────────────────────────────────────────────────────
class FbxLoader {
public:
    [[nodiscard]] static std::optional<SceneData> Load(const std::string& path);
};

} // namespace StellarAlia::Resource
