#version 450

// Separable 9-tap Gaussian blur.
// Set direction = (1,0) for horizontal pass, (0,1) for vertical pass.

layout(set = 1, binding = 0) uniform sampler2D t_Input;

layout(push_constant) uniform PushConstants {
    float dirX;
    float dirY;
    float _pad0;
    float _pad1;
} pc;

layout(location = 0) in  vec2 v_UV;
layout(location = 0) out vec4 out_Color;

// σ ≈ 1.4 Gaussian weights for offsets 0..4
const float kWeights[5] = float[](0.227027, 0.194595, 0.121622, 0.054054, 0.016216);

void main() {
    vec2 texelSize = vec2(pc.dirX, pc.dirY) / vec2(textureSize(t_Input, 0));
    vec3 result    = texture(t_Input, v_UV).rgb * kWeights[0];
    for (int i = 1; i < 5; ++i) {
        vec2 offset = texelSize * float(i);
        result += texture(t_Input, v_UV + offset).rgb * kWeights[i];
        result += texture(t_Input, v_UV - offset).rgb * kWeights[i];
    }
    out_Color = vec4(result, 1.0);
}
