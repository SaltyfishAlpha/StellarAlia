#version 450

// Bloom composite — outputs bloom * strength.
// Pipeline uses Additive blend so the result is added to the existing HDR buffer.

layout(set = 1, binding = 0) uniform sampler2D t_Bloom;

layout(push_constant) uniform PushConstants {
    float strength;
    float _pad0;
    float _pad1;
    float _pad2;
} pc;

layout(location = 0) in  vec2 v_UV;
layout(location = 0) out vec4 out_Color;

void main() {
    // Half-texel clamp: mip[0] is at half the composite resolution, so v_UV landing near
    // 0 or 1 causes bilinear+REPEAT to interpolate with the opposite edge.
    vec2 ht    = 0.5 / vec2(textureSize(t_Bloom, 0));
    vec3 bloom = texture(t_Bloom, clamp(v_UV, ht, 1.0 - ht)).rgb;
    out_Color  = vec4(bloom * pc.strength, 0.0);  // alpha=0: additive blend (src+dst)
}
