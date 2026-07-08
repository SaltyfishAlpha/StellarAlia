#version 450

// Instanced solid-bone gizmo (Issue #83 X-2). One instance per bone; the unit
// octahedron below is transformed head→tail by the per-instance model matrix.
// noVertexInput = true, so gl_VertexIndex indexes the hardcoded octahedron and
// gl_InstanceIndex selects the bone from the SSBO.
struct BoneInstance {
    mat4 model;   // unit octahedron (head=origin, tail=+Z) → world
    vec4 color;   // rgba, straight
};

layout(std430, set = 2, binding = 0) readonly buffer IB {
    BoneInstance instances[];
};

layout(push_constant) uniform PC {
    mat4 viewProj;
} pc;

layout(location = 0) out vec3 v_Normal;
layout(location = 1) out vec4 v_Color;

// Unit octahedral bone: head apex at origin, tail apex at +Z=1, widest "collar"
// ring at z=0.14 (matches Blender/Maya). 8 triangles, expanded to 24 verts.
const vec3 H = vec3( 0.0,  0.0, 0.0);
const vec3 T = vec3( 0.0,  0.0, 1.0);
const vec3 A = vec3( 1.0,  0.0, 0.14);   // +x
const vec3 B = vec3( 0.0,  1.0, 0.14);   // +y
const vec3 C = vec3(-1.0,  0.0, 0.14);   // -x
const vec3 D = vec3( 0.0, -1.0, 0.14);   // -y

const vec3 kPos[24] = vec3[](
    H, A, B,   H, B, C,   H, C, D,   H, D, A,   // head cap
    T, B, A,   T, C, B,   T, D, C,   T, A, D    // tail cap
);

void main() {
    BoneInstance inst = instances[gl_InstanceIndex];

    // Flat normal from the triangle this vertex belongs to.
    int  base = (gl_VertexIndex / 3) * 3;
    vec3 nLocal = normalize(cross(kPos[base + 1] - kPos[base],
                                  kPos[base + 2] - kPos[base]));

    vec4 world = inst.model * vec4(kPos[gl_VertexIndex], 1.0);
    gl_Position = pc.viewProj * world;

    // Inverse-transpose handles the bone's non-uniform (thin) scale.
    mat3 nmat = transpose(inverse(mat3(inst.model)));
    v_Normal  = normalize(nmat * nLocal);
    v_Color   = inst.color;
}
