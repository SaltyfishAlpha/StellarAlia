// GPU skinning helpers — include in any skinned vertex shader.
// Requires set=3 bindings (Issue #72 — skin lives at highest set so per-entity
// changes don't cascade-invalidate lower sets). Only include where a skinned
// descriptor set is bound.

struct SkinVertex { uvec4 joints; vec4 weights; };
layout(std430, set = 3, binding = 0) readonly buffer SkinMatrices { mat4       u_Bones[];     };
layout(std430, set = 3, binding = 1) readonly buffer SkinData     { SkinVertex u_SkinVerts[]; };

// Returns the blended skin matrix for the current vertex.
mat4 SkinMatrix() {
    SkinVertex sv = u_SkinVerts[gl_VertexIndex];
    return sv.weights.x * u_Bones[sv.joints.x]
         + sv.weights.y * u_Bones[sv.joints.y]
         + sv.weights.z * u_Bones[sv.joints.z]
         + sv.weights.w * u_Bones[sv.joints.w];
}
