#version 450

// Instanced solid joint marker (Issue #83 X-2). A subdivided unit icosphere
// (generated on the CPU, uploaded once) is drawn once per skeleton joint.
// Reuses debug_bone.frag (same varyings). noVertexInput = true:
//   binding 0 (instances) — per-joint transform, indexed by gl_InstanceIndex
//   binding 1 (mesh)      — unit-sphere vertex positions, by gl_VertexIndex
// Joints use uniform scale, so a unit-sphere position doubles as its (smooth)
// normal — the interpolated normals give a smooth shaded ball.
struct JointInstance {
    mat4 model;   // translate(center) * uniformScale(radius)
    vec4 color;   // straight rgba
};

layout(std430, set = 2, binding = 0) readonly buffer IB {
    JointInstance instances[];
};
layout(std430, set = 2, binding = 1) readonly buffer MB {
    vec4 meshVerts[];   // xyz = unit-sphere position (w unused)
};

layout(push_constant) uniform PC {
    mat4 viewProj;
} pc;

layout(location = 0) out vec3 v_Normal;
layout(location = 1) out vec4 v_Color;

void main() {
    JointInstance inst = instances[gl_InstanceIndex];
    vec3 lp = meshVerts[gl_VertexIndex].xyz;

    vec4 world = inst.model * vec4(lp, 1.0);
    gl_Position = pc.viewProj * world;

    // Uniform scale → mat3(model) preserves direction; smooth per-vertex normal.
    v_Normal = normalize(mat3(inst.model) * lp);
    v_Color  = inst.color;
}
