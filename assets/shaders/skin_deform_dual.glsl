// Dual-pose GPU skinning helpers — used by velocity_prepass_skinned.vert (Issue #84).
// Requires set=3 bindings:
//   binding 0 = curr-frame skin matrices (same as skin_deform.glsl)
//   binding 1 = per-vertex SkinVertex (joints + weights, shared from GPUMesh)
//   binding 2 = prev-frame skin matrices (NEW — AnimationSystem double-buffer)
//
// IMPORTANT: do NOT include this in shaders that already include skin_deform.glsl —
// the two re-declare bindings 0/1 with the same names, which clashes.

struct SkinVertex { uvec4 joints; vec4 weights; };
layout(std430, set = 3, binding = 0) readonly buffer SkinMatricesCurr { mat4       u_BonesCurr[];  };
layout(std430, set = 3, binding = 1) readonly buffer SkinData         { SkinVertex u_SkinVerts[];  };
layout(std430, set = 3, binding = 2) readonly buffer SkinMatricesPrev { mat4       u_BonesPrev[];  };

mat4 SkinMatrix() {
    SkinVertex sv = u_SkinVerts[gl_VertexIndex];
    return sv.weights.x * u_BonesCurr[sv.joints.x]
         + sv.weights.y * u_BonesCurr[sv.joints.y]
         + sv.weights.z * u_BonesCurr[sv.joints.z]
         + sv.weights.w * u_BonesCurr[sv.joints.w];
}

mat4 SkinMatrixPrev() {
    SkinVertex sv = u_SkinVerts[gl_VertexIndex];
    return sv.weights.x * u_BonesPrev[sv.joints.x]
         + sv.weights.y * u_BonesPrev[sv.joints.y]
         + sv.weights.z * u_BonesPrev[sv.joints.z]
         + sv.weights.w * u_BonesPrev[sv.joints.w];
}
