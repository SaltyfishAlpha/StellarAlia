#version 450

// Two-pass separable dilation: this pass does vertical max-dilation on the
// horizontally-dilated intermediate and composites the outline onto the swapchain.
layout(set = 1, binding = 0) uniform sampler2D t_dilateH; // horizontal-dilated mask
layout(set = 1, binding = 1) uniform sampler2D t_mask;    // original mask (center test)

layout(push_constant) uniform PC {
    vec4  texelSize;    // .xy = (1/width, 1/height)
    vec4  outlineColor;
    float outlineWidth; // dilation radius in pixels
} pc;

layout(location = 0) in  vec2 v_UV;
layout(location = 0) out vec4 o_color;

void main() {
    float center = texture(t_mask, v_UV).r;
    if (center >= 0.5) {
        o_color = vec4(0.0);
        return;
    }

    int   radius      = int(pc.outlineWidth + 0.5);
    float maxNeighbor = 0.0;
    for (int dy = -radius; dy <= radius; ++dy)
        maxNeighbor = max(maxNeighbor,
            texture(t_dilateH, v_UV + vec2(0.0, float(dy)) * pc.texelSize.xy).r);

    o_color = maxNeighbor > 0.5 ? pc.outlineColor : vec4(0.0);
}
