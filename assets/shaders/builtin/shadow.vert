#version 450

// Depth-only pass: only needs position + model matrix.
// No set=0 needed — shadow pass uses its own camera (light-space).

layout(location = 0) in vec3 a_Position;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 lightViewProj;
} pc;

void main() {
    gl_Position = pc.lightViewProj * pc.model * vec4(a_Position, 1.0);
}
