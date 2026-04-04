#version 450

// 3×3 tent filter upsample.
// Output is additively blended onto the render target by the pipeline,
// accumulating the finer-mip contribution on top of the coarser mip content.

layout(set = 1, binding = 0) uniform sampler2D t_Input;

layout(push_constant) uniform PushConstants {
    float radius;   // filter spread in source texels (default 1.0)
    float _p0, _p1, _p2;
} pc;

layout(location = 0) in  vec2 v_UV;
layout(location = 0) out vec4 out_Color;

void main() {
    vec2 ts = pc.radius / vec2(textureSize(t_Input, 0));

    // 3×3 tent weights: corners=1, edges=2, center=4  (sum=16)
    vec3 result =
        texture(t_Input, v_UV + ts * vec2(-1.0, -1.0)).rgb * 1.0 +
        texture(t_Input, v_UV + ts * vec2( 0.0, -1.0)).rgb * 2.0 +
        texture(t_Input, v_UV + ts * vec2( 1.0, -1.0)).rgb * 1.0 +
        texture(t_Input, v_UV + ts * vec2(-1.0,  0.0)).rgb * 2.0 +
        texture(t_Input, v_UV                         ).rgb * 4.0 +
        texture(t_Input, v_UV + ts * vec2( 1.0,  0.0)).rgb * 2.0 +
        texture(t_Input, v_UV + ts * vec2(-1.0,  1.0)).rgb * 1.0 +
        texture(t_Input, v_UV + ts * vec2( 0.0,  1.0)).rgb * 2.0 +
        texture(t_Input, v_UV + ts * vec2( 1.0,  1.0)).rgb * 1.0;

    out_Color = vec4(result / 16.0, 1.0);
}
