#version 450

// Fullscreen triangle trick — no vertex buffer required.
// Draw with vkCmdDraw(cmd, 3, 1, 0, 0).
//
//  NDC coords:         UV:
//  (-1,-1) ─── (3,-1)  (0,0) ─── (2,0)
//     │                  │
//  (-1, 3)              (0,2)

layout(location = 0) out vec2 v_UV;

void main() {
    v_UV        = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(v_UV * 2.0 - 1.0, 0.0, 1.0);
}
