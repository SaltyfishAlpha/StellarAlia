#ifndef SPD_REDUCE_GLSL
#define SPD_REDUCE_GLSL

// Single-pass downsampler (SPD) core — Issue #94. Shared by every SPD consumer:
//   spd_downsample.comp (default 4-way average) and hiz_spd.comp (min, Issue #89).
// A consumer .comp provides `#version` + the include-directive extension, optionally
// `#define SPD_REDUCE(a,b,c,d)` before including this, then nothing else.
//
// One dispatch generates a mip chain by 2x2 hierarchical reduction. Each 256-thread
// workgroup owns a 64x64 source tile and reduces it in registers + LDS to mip1..mip6
// (a 64x64 tile collapses to one mip6 texel); tiles are independent, so a single
// dispatch produces the whole mip1..mip6 with no cross-workgroup traffic. mip7..mip12
// combine every tile's mip6 texel: the last workgroup to finish (elected via a global
// atomic counter, `coherent` for cross-workgroup visibility) reduces the mip6 image.
// LDS-only (no subgroup ops) → compiles at the default vulkan1.0 target. Source up to
// 4096x4096 (12 mips) in one dispatch. No per-subresource render-graph tracking needed.

layout(local_size_x = 16, local_size_y = 16) in;

// mip0 source. Default: a separate sampled texture (SHADER_READ). Consumers whose mip0
// lives in the same chain they write (in-place, e.g. Hi-Z) define SPD_SRC_IMAGE to read
// it via imageLoad instead — a sampler descriptor would mismatch the chain's GENERAL layout.
#ifdef SPD_SRC_IMAGE
layout(set = 0, binding = 0, r32f) readonly uniform image2D u_src;
#else
layout(set = 0, binding = 0)                uniform sampler2D t_src;
#endif
layout(set = 0, binding = 1, r32f) coherent uniform image2D  u_mips[12];  // mip1..mip12
layout(set = 0, binding = 2) coherent buffer SpdGlobal { uint counter; } spd;

layout(push_constant) uniform PC {
    ivec2 srcSize;        // mip0 dimensions
    int   mipCount;       // number of output mips to produce (1..12)
    int   numWorkGroups;  // total workgroups in the local pass = dispatch.x * dispatch.y
} pc;

#ifndef SPD_REDUCE
#define SPD_REDUCE(a, b, c, d) (((a) + (b) + (c) + (d)) * 0.25)
#endif

shared float s_tile[16][16];
shared bool  s_lastGroup;

// srcMip < 0 → sample mip0 via t_src; else imageLoad a previously-written mip.
float FetchSrc(ivec2 c, ivec2 size, int srcMip) {
    c = clamp(c, ivec2(0), size - ivec2(1));
    if (srcMip < 0) {
#ifdef SPD_SRC_IMAGE
        return imageLoad(u_src, c).r;
#else
        return texelFetch(t_src, c, 0).r;
#endif
    }
    return imageLoad(u_mips[srcMip], c).r;
}

// Reduce one 64x64 tile at `origin` (in source-of-`srcMip` coords, sized `srcSize`)
// to up to 6 levels, writing level k into u_mips[outBase + k] at (tileGrid*dim(k) + local).
void ReduceTile(ivec2 origin, ivec2 srcSize, int srcMip, int outBase, ivec2 tileGrid,
                int maxOut) {
    ivec2 lid = ivec2(gl_LocalInvocationID.xy);   // [0,16)

    // level 0 (mip = outBase): each thread reduces a 4x4 block → 2x2 outputs.
    float m1[2][2];
    for (int a = 0; a < 2; ++a)
        for (int b = 0; b < 2; ++b) {
            ivec2 s = origin + lid * 4 + ivec2(b * 2, a * 2);
            m1[a][b] = SPD_REDUCE(FetchSrc(s + ivec2(0, 0), srcSize, srcMip),
                                  FetchSrc(s + ivec2(1, 0), srcSize, srcMip),
                                  FetchSrc(s + ivec2(0, 1), srcSize, srcMip),
                                  FetchSrc(s + ivec2(1, 1), srcSize, srcMip));
            imageStore(u_mips[outBase], tileGrid * 32 + lid * 2 + ivec2(b, a), vec4(m1[a][b]));
        }
    if (maxOut <= 1) return;

    // level 1 (mip = outBase+1): reduce the 2x2 → 1, seed LDS (16x16).
    float m2 = SPD_REDUCE(m1[0][0], m1[0][1], m1[1][0], m1[1][1]);
    imageStore(u_mips[outBase + 1], tileGrid * 16 + lid, vec4(m2));
    s_tile[lid.y][lid.x] = m2;

    // levels 2..5 (mip = outBase+2 .. outBase+5): successive LDS 2x2 reductions.
    int ldsDim = 16;
    for (int k = 2; k < 6 && k < maxOut; ++k) {
        barrier();
        int  od       = ldsDim >> 1;              // 8,4,2,1
        bool doReduce = lid.x < od && lid.y < od;
        float r = 0.0;
        if (doReduce) {
            ivec2 s = lid * 2;
            r = SPD_REDUCE(s_tile[s.y][s.x], s_tile[s.y][s.x + 1],
                           s_tile[s.y + 1][s.x], s_tile[s.y + 1][s.x + 1]);
            imageStore(u_mips[outBase + k], tileGrid * od + lid, vec4(r));
        }
        barrier();
        if (doReduce) s_tile[lid.y][lid.x] = r;
        ldsDim = od;
    }
}

void main() {
    ivec2 wg = ivec2(gl_WorkGroupID.xy);

    // Local pass: this workgroup's 64x64 tile → mip1..mip6.
    ReduceTile(wg * 64, pc.srcSize, /*srcMip=*/-1, /*outBase=*/0, wg, min(pc.mipCount, 6));

    if (pc.mipCount <= 6) return;

    // Global pass: last workgroup reduces the mip6 image → mip7..
    memoryBarrierImage();
    barrier();
    if (gl_LocalInvocationIndex == 0u) {
        uint prev = atomicAdd(spd.counter, 1u);
        s_lastGroup = (prev == uint(pc.numWorkGroups - 1));
    }
    barrier();
    if (!s_lastGroup) return;

    if (gl_LocalInvocationIndex == 0u) spd.counter = 0u;   // reset for next dispatch
    memoryBarrierBuffer();

    ivec2 m6size = (pc.srcSize + ivec2(63)) / ivec2(64);
    ReduceTile(ivec2(0), m6size, /*srcMip=*/5, /*outBase=*/6, ivec2(0), pc.mipCount - 6);
}

#endif // SPD_REDUCE_GLSL
