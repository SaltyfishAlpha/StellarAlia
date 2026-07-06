#version 450

// X-8 / Issue #102 debug view: visualize the ID buffer as per-submesh colors.
// texelFetch bypasses sampler filtering, so the UINT texture needs no
// NEAREST-only sampler special-casing.
layout(set = 2, binding = 0) uniform usampler2D u_Id;

layout(location = 0) out vec4 o_color;

vec3 HashColor(uint id) {
    // Wang hash — stable, well-distributed color per id.
    uint h = id;
    h = (h ^ 61u) ^ (h >> 16); h *= 9u; h = h ^ (h >> 4);
    h *= 0x27d4eb2du; h = h ^ (h >> 15);
    return vec3(float( h         & 255u),
                float((h >>  8u) & 255u),
                float((h >> 16u) & 255u)) / 255.0;
}

void main() {
    uint id = texelFetch(u_Id, ivec2(gl_FragCoord.xy), 0).x;
    o_color = (id == 0u) ? vec4(0.08, 0.08, 0.10, 1.0)
                         : vec4(HashColor(id), 1.0);
}
