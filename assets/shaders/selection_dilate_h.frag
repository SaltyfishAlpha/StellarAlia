#version 450

layout(set = 2, binding = 0) uniform sampler2D t_mask;

layout(push_constant) uniform PC {
    vec4  texelSize;    // .xy = (1/width, 1/height)
    vec4  outlineColor;
    float outlineWidth; // dilation radius in pixels
} pc;

layout(location = 0) in  vec2 v_UV;
layout(location = 0) out vec4 o_color;

void main() {
    int   radius = int(pc.outlineWidth + 0.5);
    float maxVal = 0.0;
    for (int dx = -radius; dx <= radius; ++dx)
        maxVal = max(maxVal, texture(t_mask, v_UV + vec2(float(dx), 0.0) * pc.texelSize.xy).r);
    o_color = vec4(maxVal, 0.0, 0.0, 0.0);
}
