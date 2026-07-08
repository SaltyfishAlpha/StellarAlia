#pragma once

#include <optional>
#include <string>

#include "resource/types/MeshData.hpp"

namespace StellarAlia::Resource {

// ─────────────────────────────────────────────────────────────────────────────
// ObjLoader — Wavefront OBJ + MTL into SceneData (Issue #108, tinyobjloader).
//
// Produces a single mesh under one identity root node; primitives are split
// per (shape × material). MTL maps to a PBR approximation: Kd/map_Kd →
// baseColor, map_Bump/norm → normal, Ns → roughness (Pr/Pm honored when the
// MTL uses the PBR extension), Ke/map_Ke → emissive, d<1 → BLEND.
// OBJ carries no skin/animation and no tangents; normals are recomputed when
// absent and tangents always come from MeshUtils (MikkTSpace).
// ─────────────────────────────────────────────────────────────────────────────
class ObjLoader {
public:
    [[nodiscard]] static std::optional<SceneData> Load(const std::string& path);
};

} // namespace StellarAlia::Resource
