#version 450

// TileMax (Issue #46): 16×16 downscale, keep velocity vector with largest length
// in each tile. Output resolution = ceil(w/16) × ceil(h/16).

layout(set = 2, binding = 0) uniform sampler2D t_Velocity;

layout(location = 0) in  vec2 v_TexCoord;
layout(location = 0) out vec2 out_TileMax;

layout(push_constant) uniform PC {
    int tileSize;          // = 16
    int _pad0;
    int _pad1;
    int _pad2;
};

void main() {
    ivec2 srcSize  = textureSize(t_Velocity, 0);
    ivec2 tileBase = ivec2(gl_FragCoord.xy) * tileSize;

    vec2  maxV   = vec2(0.0);
    float maxLen = 0.0;
    for (int y = 0; y < tileSize; ++y) {
        for (int x = 0; x < tileSize; ++x) {
            ivec2 p = tileBase + ivec2(x, y);
            if (p.x >= srcSize.x || p.y >= srcSize.y) continue;
            vec2  v = texelFetch(t_Velocity, p, 0).rg;
            float l = dot(v, v);  // squared length is enough for comparison
            if (l > maxLen) { maxLen = l; maxV = v; }
        }
    }
    out_TileMax = maxV;
}
