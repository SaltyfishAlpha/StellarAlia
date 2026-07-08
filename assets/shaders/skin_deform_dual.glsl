// Dual-pose GPU skinning helpers — used by velocity_prepass_skinned.vert (Issue #84).
// Requires set=3 bindings:
//   binding 0 = curr-frame skin matrices (same as skin_deform.glsl)
//   binding 1 = per-vertex SkinVertex (joints + weights, shared from GPUMesh)
//   binding 2 = prev-frame skin matrices (NEW — AnimationSystem double-buffer)
//
// IMPORTANT: do NOT include this in shaders that already include skin_deform.glsl —
// the two re-declare bindings 0/1 with the same names, which clashes.
//
// #83 P1: matrices stored compressed as mat3x4 rows — see skin_deform.glsl.

struct SkinVertex { uvec4 joints; vec4 weights; };
layout(std430, set = 3, binding = 0) readonly buffer SkinMatricesCurr { mat3x4     u_BonesCurr[];  };
layout(std430, set = 3, binding = 1) readonly buffer SkinData         { SkinVertex u_SkinVerts[];  };
layout(std430, set = 3, binding = 2) readonly buffer SkinMatricesPrev { mat3x4     u_BonesPrev[];  };

mat4 SkinRowsToMat4(mat3x4 rows) {
    return transpose(mat4(rows[0], rows[1], rows[2], vec4(0.0, 0.0, 0.0, 1.0)));
}

mat4 SkinMatrix() {
    SkinVertex sv = u_SkinVerts[gl_VertexIndex];
    mat3x4 rows = sv.weights.x * u_BonesCurr[sv.joints.x]
                + sv.weights.y * u_BonesCurr[sv.joints.y]
                + sv.weights.z * u_BonesCurr[sv.joints.z]
                + sv.weights.w * u_BonesCurr[sv.joints.w];
    return SkinRowsToMat4(rows);
}

mat4 SkinMatrixPrev() {
    SkinVertex sv = u_SkinVerts[gl_VertexIndex];
    mat3x4 rows = sv.weights.x * u_BonesPrev[sv.joints.x]
                + sv.weights.y * u_BonesPrev[sv.joints.y]
                + sv.weights.z * u_BonesPrev[sv.joints.z]
                + sv.weights.w * u_BonesPrev[sv.joints.w];
    return SkinRowsToMat4(rows);
}
