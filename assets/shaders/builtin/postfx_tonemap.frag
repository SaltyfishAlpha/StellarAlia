#version 450

layout(set = 1, binding = 0) uniform sampler2D t_HDR;

layout(push_constant) uniform PushConstants {
    float exposure;
    float gamma;
    float _pad0;
    float _pad1;
} pc;

layout(location = 0) in  vec2 v_UV;
layout(location = 0) out vec4 out_Color;

// Filmic tone curve (ACES approximation by Krzysztof Narkowicz)
vec3 ACESFilmic(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

void main() {
    vec3 hdr        = texture(t_HDR, v_UV).rgb * pc.exposure;
    vec3 tonemapped = ACESFilmic(hdr);
    // No manual gamma: swapchain is VK_FORMAT_B8G8R8A8_SRGB,
    // the driver applies linear→sRGB automatically.
    out_Color = vec4(tonemapped, 1.0);
}
