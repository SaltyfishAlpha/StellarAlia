#version 450
#extension GL_GOOGLE_include_directive : enable
#include "frame_uniforms.glsl"
#include "common.glsl"  // OctDecode, PI

// set=1 inputs
layout(set = 2, binding = 0) uniform sampler2D t_GDepth;
layout(set = 2, binding = 1) uniform sampler2D t_GNormalMaterial;

layout(location = 0) in  vec2 v_TexCoord;
layout(location = 0) out float out_AO;

layout(push_constant) uniform PC {
    float enabled;      // 0.0 → skip (write 1.0)
    float radius;
    float strength;
    float bias;
    int   directions;
    int   steps;
    float _pad0;
    float _pad1;
};

// ── View-space position from uv ───────────────────────────────────────────────
// Uses invProj directly — one mat4×vec4 instead of invViewProj+view (two mats).
vec3 ViewPos(vec2 uv) {
    float d   = texture(t_GDepth, uv).r;
    vec4  clip = vec4(uv * 2.0 - 1.0, d, 1.0);
    vec4  vh   = u_Frame.invProj * clip;
    return vh.xyz / vh.w;
}

// ── Horizon-Based AO (HBAO) with N directions ─────────────────────────────────
//
// For each screen-space direction φ_i:
//   1. Project the surface normal onto the slice plane (spanned by dir + view axis).
//   2. Scan ±dir to find max horizon angles h+ / h- (sin of elevation).
//   3. AO contribution = max(0, h - sin(n_proj)) × |N_slice| for both sides.
//
// Reference: Bavoil & Sainz "Image-Space Horizon-Based AO" GDC 2008;
//            Jimenez et al. SIGGRAPH 2016 (analytical bent normal extension).

void main() {
    if (enabled < 0.5) { out_AO = 1.0; return; }

    float depth = texture(t_GDepth, v_TexCoord).r;
    if (depth >= 1.0 - 1e-5) { out_AO = 1.0; return; }

    vec3 P = ViewPos(v_TexCoord);

    vec4 nm = texture(t_GNormalMaterial, v_TexCoord);
    vec3 worldN = OctDecode(nm.rg);
    vec3 N = normalize(mat3(u_Frame.view) * worldN);

    vec2 texelSize = 1.0 / vec2(textureSize(t_GDepth, 0));

    float ao = 0.0;

    // Per-frame direction rotation: each frame offsets the entire slice set by one step
    // so TAA accumulation covers more unique angles (effectively directions × frames samples).
    float stepAngle = PI / float(max(directions, 1));
    float phiOffset = float(u_Frame.frameIndex) * stepAngle;

    for (int d = 0; d < directions; ++d) {
        float phi = phiOffset + stepAngle * (float(d) + 0.5);
        vec2 dir2D = vec2(cos(phi), sin(phi));
        vec3 sliceTangent = vec3(dir2D, 0.0);

        // Project N onto the slice plane.
        // Slice plane normal = cross(sliceTangent, viewDir=(0,0,-1)).
        vec3 sliceNorm  = normalize(cross(sliceTangent, vec3(0.0, 0.0, -1.0)));
        vec3 N_slice    = N - dot(N, sliceNorm) * sliceNorm;
        float nLen      = length(N_slice);
        // sin(n_angle) = N_slice.z / |N_slice|  (elevation of projected normal)
        float sinN = (nLen > 1e-4) ? N_slice.z / nLen : 0.0;

        float h_pos = -1.0;  // sin of max horizon in +dir, init at nadir
        float h_neg = -1.0;  // sin of max horizon in -dir

        for (int s = 1; s <= steps; ++s) {
            float t = (float(s) / float(steps)) * radius;

            vec2 uv1 = clamp(v_TexCoord + dir2D * t * texelSize, vec2(0.0), vec2(1.0));
            vec3 S1  = ViewPos(uv1) - P;
            float l1 = length(S1);
            if (l1 > 1e-4) h_pos = max(h_pos, S1.z / l1);

            vec2 uv2 = clamp(v_TexCoord - dir2D * t * texelSize, vec2(0.0), vec2(1.0));
            vec3 S2  = ViewPos(uv2) - P;
            float l2 = length(S2);
            if (l2 > 1e-4) h_neg = max(h_neg, S2.z / l2);
        }

        float sinBias = bias;
        ao += (max(0.0, h_pos - sinN - sinBias) + max(0.0, h_neg - sinN - sinBias)) * nLen;
    }

    // Normalize: 2 sides × directions; strength controls overall intensity.
    ao = clamp(ao * strength / (2.0 * float(directions)), 0.0, 1.0);
    out_AO = 1.0 - ao;
}
