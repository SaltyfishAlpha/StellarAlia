// GPU skinning helpers — include in any skinned vertex shader.
// Requires set=3 bindings (Issue #72 — skin lives at highest set so per-entity
// changes don't cascade-invalidate lower sets). Only include where a skinned
// descriptor set is bound.
//
// #83 P1: bone matrices are stored compressed as mat3x4 — the three ROWS of
// the affine mat4 (48 B/bone instead of 64, ~25% palette bandwidth saved).
// Rows blend linearly exactly like the full matrix; the implicit fourth row
// is (0,0,0,1).

struct SkinVertex { uvec4 joints; vec4 weights; };
layout(std430, set = 3, binding = 0) readonly buffer SkinMatrices { mat3x4     u_Bones[];     };
layout(std430, set = 3, binding = 1) readonly buffer SkinData     { SkinVertex u_SkinVerts[]; };

mat4 SkinRowsToMat4(mat3x4 rows) {
    return transpose(mat4(rows[0], rows[1], rows[2], vec4(0.0, 0.0, 0.0, 1.0)));
}

// Returns the blended skin matrix for the current vertex.
mat4 SkinMatrix() {
    SkinVertex sv = u_SkinVerts[gl_VertexIndex];
    mat3x4 rows = sv.weights.x * u_Bones[sv.joints.x]
                + sv.weights.y * u_Bones[sv.joints.y]
                + sv.weights.z * u_Bones[sv.joints.z]
                + sv.weights.w * u_Bones[sv.joints.w];
    return SkinRowsToMat4(rows);
}
