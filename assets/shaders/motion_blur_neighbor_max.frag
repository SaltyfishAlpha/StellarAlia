#version 450

// NeighborMax (Issue #46): 3×3 dilate over tile_max, keeps the strongest
// velocity vector among the 9 neighbours. Avoids tile-boundary popping when
// the centre tile happens to be in a slow region next to a fast one.

layout(set = 2, binding = 0) uniform sampler2D t_TileMax;

layout(location = 0) in  vec2 v_TexCoord;
layout(location = 0) out vec2 out_NeighborMax;

void main() {
    ivec2 size   = textureSize(t_TileMax, 0);
    ivec2 center = ivec2(gl_FragCoord.xy);

    vec2  maxV   = vec2(0.0);
    float maxLen = 0.0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            ivec2 p = clamp(center + ivec2(dx, dy), ivec2(0), size - ivec2(1));
            vec2  v = texelFetch(t_TileMax, p, 0).rg;
            float l = dot(v, v);
            if (l > maxLen) { maxLen = l; maxV = v; }
        }
    }
    out_NeighborMax = maxV;
}
