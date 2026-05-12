#version 450

// Vertex data lives in a CPU-visible SSBO written by DebugDraw each frame.
// noVertexInput = true in the pipeline, so gl_VertexIndex indexes into the SSBO.
struct DebugVertex {
    float px, py, pz;   // position  (3 × 4 = 12 bytes)
    uint  color;         // packed RGBA8 (4 bytes) — total 16 bytes, std430-compatible
};

layout(std430, set = 2, binding = 0) readonly buffer VB {
    DebugVertex verts[];
};

layout(push_constant) uniform PC {
    mat4 viewProj;
} pc;

layout(location=0) out vec4 v_Color;

void main() {
    DebugVertex v = verts[gl_VertexIndex];
    gl_Position = pc.viewProj * vec4(v.px, v.py, v.pz, 1.0);
    v_Color = vec4(
        float( v.color        & 0xFFu) / 255.0,
        float((v.color >>  8) & 0xFFu) / 255.0,
        float((v.color >> 16) & 0xFFu) / 255.0,
        float((v.color >> 24) & 0xFFu) / 255.0
    );
}
