#version 450

// LUT-based tonemap + color grading.
//
// Pipeline:
//   1. Apply ACES filmic tonemap to HDR input.
//   2. Apply color grading via a 2D horizontal-strip LUT.
//
// LUT format — 2D strip, (N*N) × N pixels:
//   width  = N*N  (e.g. 256 for N=16)
//   height = N    (e.g.  16 for N=16)
//   R → x within a tile,  G → y,  B → which tile (horizontal index).
// Trilinear interpolation is done manually across adjacent blue tiles.

layout(set = 2, binding = 0) uniform sampler2D t_HDR;
layout(set = 2, binding = 1) uniform sampler2D t_LUT;

layout(push_constant) uniform PushConstants {
    float exposure;
    float lutStrength;   // blend [0=no grading, 1=full grading]
    float _pad0;
    float _pad1;
} pc;

layout(location = 0) in  vec2 v_UV;
layout(location = 0) out vec4 out_Color;

// ACES filmic approximation (Narkowicz 2015)
vec3 ACESFilmic(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Horizontal-strip LUT lookup with trilinear interpolation.
vec3 ApplyLUT(vec3 color) {
    ivec2 sz = textureSize(t_LUT, 0);   // e.g. (256, 16)
    float N  = float(sz.y);             // number of slices = LUT dimension

    color = clamp(color, 0.0, 1.0);

    // Blue axis: fractional slice index
    float blueF = color.b * (N - 1.0);
    float b0    = floor(blueF);
    float b1    = min(b0 + 1.0, N - 1.0);
    float bfrac = blueF - b0;

    // UV within the strip for the two neighbouring blue slices.
    // Each slice occupies a 1/N horizontal band.
    float x0 = (b0 + color.r * (N - 1.0) / N + 0.5 / float(sz.x)) / N;
    float x1 = (b1 + color.r * (N - 1.0) / N + 0.5 / float(sz.x)) / N;
    float y  =        color.g * (N - 1.0) / N + 0.5 / N;

    vec3 c0 = texture(t_LUT, vec2(x0, y)).rgb;
    vec3 c1 = texture(t_LUT, vec2(x1, y)).rgb;
    return mix(c0, c1, bfrac);
}

void main() {
    vec3 hdr        = texture(t_HDR, v_UV).rgb * pc.exposure;
    vec3 tonemapped = ACESFilmic(hdr);
    vec3 graded     = ApplyLUT(tonemapped);
    out_Color = vec4(mix(tonemapped, graded, pc.lutStrength), 1.0);
}
