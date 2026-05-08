#version 450

layout(set = 0, binding = 0) uniform sampler2D t_Source;

layout(location = 0) in  vec2 v_UV;
layout(location = 0) out vec4 out_Color;

void main() {
    out_Color = texture(t_Source, v_UV);
}
