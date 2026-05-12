#version 450

// Bright-pass threshold with 13-tap downsampling + Karis Average.
//
// 13-tap pattern (offset in source texels):
//   a(-2,-2)  b( 0,-2)  c(+2,-2)
//     d(-1,-1)    e(+1,-1)
//   f(-2, 0)  g( 0, 0)  h(+2, 0)
//     i(-1,+1)    j(+1,+1)
//   k(-2,+2)  l( 0,+2)  m(+2,+2)
//
// Decomposed into five overlapping 2×2 groups:
//   inner  {d,e,i,j}  weight 0.500
//   TL     {a,b,f,g}  weight 0.125
//   TR     {b,c,g,h}  weight 0.125
//   BL     {f,g,k,l}  weight 0.125
//   BR     {g,h,l,m}  weight 0.125
//
// Karis Average: each group is weighted by 1/(1+luminance) per sample,
// preventing isolated very-bright pixels from dominating the box average
// (eliminates temporal firefly flicker at the first HDR downsample).

layout(set = 2, binding = 0) uniform sampler2D t_HDR;

layout(push_constant) uniform PushConstants {
    float threshold;
    float knee;
    float _pad0;
    float _pad1;
} pc;

layout(location = 0) in  vec2 v_UV;
layout(location = 0) out vec4 out_Color;

float Luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// Clamp to half-texel centres so bilinear+REPEAT never interpolates across the edge.
// clamp(uv, 0, 1) is insufficient: at UV=0 or 1 bilinear still wraps to the opposite edge.
vec3 S(vec2 uv) {
    vec2 ht = 0.5 / vec2(textureSize(t_HDR, 0));
    return texture(t_HDR, clamp(uv, ht, 1.0 - ht)).rgb;
}

// Karis-weighted average of a 2×2 group.
vec3 KarisBox(vec3 a, vec3 b, vec3 c, vec3 d) {
    float wa = 1.0 / (1.0 + Luma(a));
    float wb = 1.0 / (1.0 + Luma(b));
    float wc = 1.0 / (1.0 + Luma(c));
    float wd = 1.0 / (1.0 + Luma(d));
    return (a*wa + b*wb + c*wc + d*wd) / (wa + wb + wc + wd);
}

void main() {
    vec2 ts = 1.0 / vec2(textureSize(t_HDR, 0));

    vec3 a = S(v_UV + ts * vec2(-2,-2));
    vec3 b = S(v_UV + ts * vec2( 0,-2));
    vec3 c = S(v_UV + ts * vec2( 2,-2));
    vec3 d = S(v_UV + ts * vec2(-1,-1));
    vec3 e = S(v_UV + ts * vec2( 1,-1));
    vec3 f = S(v_UV + ts * vec2(-2, 0));
    vec3 g = S(v_UV + ts * vec2( 0, 0));
    vec3 h = S(v_UV + ts * vec2( 2, 0));
    vec3 i = S(v_UV + ts * vec2(-1, 1));
    vec3 j = S(v_UV + ts * vec2( 1, 1));
    vec3 k = S(v_UV + ts * vec2(-2, 2));
    vec3 l = S(v_UV + ts * vec2( 0, 2));
    vec3 m = S(v_UV + ts * vec2( 2, 2));

    vec3 color = KarisBox(d, e, i, j) * 0.500
               + KarisBox(a, b, f, g) * 0.125
               + KarisBox(b, c, g, h) * 0.125
               + KarisBox(f, g, k, l) * 0.125
               + KarisBox(g, h, l, m) * 0.125;

    // Soft-knee threshold (unchanged from original)
    float brightness = Luma(color);
    float rq     = clamp(brightness - pc.threshold + pc.knee, 0.0, 2.0 * pc.knee);
    rq           = (rq * rq) / (4.0 * pc.knee + 1e-5);
    float weight = max(rq, brightness - pc.threshold) / max(brightness, 1e-5);

    out_Color = vec4(color * weight, 1.0);
}
