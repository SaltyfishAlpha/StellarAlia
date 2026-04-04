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
    vec3 bloom = texture(t_Bloom, v_UV).rgb;
    out_Color  = vec4(bloom * pc.strength, 0.0);  // alpha=0: additive blend (src+dst)
}
