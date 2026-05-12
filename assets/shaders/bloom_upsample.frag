#version 450

// 3×3 tent filter upsample.
// Output is additively blended onto the render target by the pipeline,
// accumulating the finer-mip contribution on top of the coarser mip content.

layout(set = 2, binding = 0) uniform sampler2D t_Input;

layout(push_constant) uniform PushConstants {
    float radius;   // filter spread in source texels (default 1.0)
    float _p0, _p1, _p2;
} pc;

layout(location = 0) in  vec2 v_UV;
layout(location = 0) out vec4 out_Color;

// Clamp to half-texel centres so bilinear+REPEAT never interpolates across the edge.
vec3 S(vec2 uv) {
    vec2 ht = 0.5 / vec2(textureSize(t_Input, 0));
    return texture(t_Input, clamp(uv, ht, 1.0 - ht)).rgb;
}

void main() {
    vec2 ts = pc.radius / vec2(textureSize(t_Input, 0));

    // 3×3 tent weights: corners=1, edges=2, center=4  (sum=16)
    vec3 result =
        S(v_UV + ts * vec2(-1.0, -1.0)) * 1.0 +
        S(v_UV + ts * vec2( 0.0, -1.0)) * 2.0 +
        S(v_UV + ts * vec2( 1.0, -1.0)) * 1.0 +
        S(v_UV + ts * vec2(-1.0,  0.0)) * 2.0 +
        S(v_UV                         ) * 4.0 +
        S(v_UV + ts * vec2( 1.0,  0.0)) * 2.0 +
        S(v_UV + ts * vec2(-1.0,  1.0)) * 1.0 +
        S(v_UV + ts * vec2( 0.0,  1.0)) * 2.0 +
        S(v_UV + ts * vec2( 1.0,  1.0)) * 1.0;

    out_Color = vec4(result / 16.0, 1.0);
}
