#version 450

// 13-tap Jimenez downsample (no Karis — fireflies already suppressed at threshold).
//
// Same 5-group decomposition as the threshold pass, but weights are summed
// linearly (no per-group luminance weighting needed after the first mip):
//
//   a(-2,-2)  b( 0,-2)  c(+2,-2)
//     d(-1,-1)    e(+1,-1)
//   f(-2, 0)  g( 0, 0)  h(+2, 0)
//     i(-1,+1)    j(+1,+1)
//   k(-2,+2)  l( 0,+2)  m(+2,+2)
//
// Group weights (sum = 1):
//   inner  {d,e,i,j}  each = 0.125     (group=0.5,   4 taps)
//   TL     {a,b,f,g}  each = 0.03125   (group=0.125, 4 taps)  <- corners share g
//   TR     {b,c,g,h}  each = 0.03125
//   BL     {f,g,k,l}  each = 0.03125
//   BR     {g,h,l,m}  each = 0.03125
//
// Accumulated per-tap contribution:
//   corners  a,c,k,m        : 0.03125
//   edges    b,f,h,l        : 0.0625   (appear in two outer groups)
//   inner    d,e,i,j        : 0.125
//   center   g              : 0.125    (appears in all four outer groups)

layout(set = 1, binding = 0) uniform sampler2D t_Input;

layout(location = 0) in  vec2 v_UV;
layout(location = 0) out vec4 out_Color;

// Clamp to half-texel centres so bilinear+REPEAT never interpolates across the edge.
vec3 S(vec2 uv) {
    vec2 ht = 0.5 / vec2(textureSize(t_Input, 0));
    return texture(t_Input, clamp(uv, ht, 1.0 - ht)).rgb;
}

void main() {
    vec2 ts = 1.0 / vec2(textureSize(t_Input, 0));

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

    vec3 color =
        (d + e + i + j) * 0.125    +   // inner ring
        (b + f + h + l) * 0.0625   +   // cross edges
        (a + c + k + m) * 0.03125  +   // corners
        g               * 0.125;       // centre

    out_Color = vec4(color, 1.0);
}
