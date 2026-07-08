#pragma once

#include <optional>
#include <string>

#include "resource/types/MeshData.hpp"

namespace StellarAlia::Resource {

// ─────────────────────────────────────────────────────────────────────────────
// ModelLoader — extension dispatch to the per-format loaders (Issue #108).
//
// The single entry point for source-model parsing: every format loader
// produces the same format-agnostic SceneData, so the cook backend
// (CookMesh + material/skeleton/anim sidecars) never knows the source format.
//
//   .gltf / .glb / .vrm  → GltfLoader   (.vrm is a glb container; MToon &
//                                        humanoid extensions ignored for now)
//   .obj                 → ObjLoader
//   .fbx                 → FbxLoader
// ─────────────────────────────────────────────────────────────────────────────
class ModelLoader {
public:
    [[nodiscard]] static std::optional<SceneData> Load(const std::string& path);

    // ext with leading dot, any case (".OBJ" ok). Shared by ImportScanner and
    // the editor import dialog so the supported-format list has one home.
    [[nodiscard]] static bool SupportsExtension(std::string ext);
};

} // namespace StellarAlia::Resource
