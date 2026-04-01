#version 450

layout(set = 1, binding = 0) uniform MaterialParams {
    vec4 baseColorFactor;
} u_Mat;

layout(set = 1, binding = 1) uniform sampler2D t_BaseColor;

layout(location = 0) in  vec2 v_TexCoord0;
layout(location = 0) out vec4 out_Color;

void main() {
    out_Color = texture(t_BaseColor, v_TexCoord0) * u_Mat.baseColorFactor;
}
