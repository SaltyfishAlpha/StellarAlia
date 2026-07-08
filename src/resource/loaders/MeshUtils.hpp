#pragma once

#include <cstdint>
#include <vector>

#include "resource/types/MeshData.hpp"

namespace StellarAlia::Resource::MeshUtils {

// Area-weighted smooth normals, overwriting Vertex::normal in place. Only for
// meshes whose topology welds by position — on hard-surface meshes shared
// corners get averaged edge normals; prefer GenerateFlatNormals for those.
void GenerateNormals(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

// Faceted per-face normals for sources that carry none (OBJ without vn) —
// matches GltfLoader's missing-NORMAL behavior. Vertices are split per corner
// (vertices / indices — and skinVertices when non-null — are rewritten); a
// following GenerateTangents pass re-welds what stays identical.
void GenerateFlatNormals(std::vector<Vertex>&     vertices,
                         std::vector<uint32_t>&   indices,
                         std::vector<SkinVertex>* skinVertices = nullptr);

// MikkTSpace tangents (Issue #108). The industry-standard basis — normal maps
// baked in Blender/Substance/Unity/UE assume it, so a hand-rolled UV-gradient
// accumulation would show seams on mirrored UVs.
//
// MikkTSpace works per face-corner, so the mesh is expanded to unindexed
// corners, processed, then re-welded (bitwise dedup). vertices / indices —
// and skinVertices when non-null and parallel to vertices — are rewritten;
// vertex count may grow where tangents split along UV seams.
//
// Requires valid normals and UVs; returns false on malformed input (index
// count not a multiple of 3, out-of-range indices).
bool GenerateTangents(std::vector<Vertex>&      vertices,
                      std::vector<uint32_t>&    indices,
                      std::vector<SkinVertex>*  skinVertices = nullptr);

} // namespace StellarAlia::Resource::MeshUtils
