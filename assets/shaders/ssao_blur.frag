#version 450

// Separable 5-tap bilateral Gaussian blur for AO.
// Direction controlled by push-constant texelStep (vec2).
// Depth-aware weight: exp(-|d_i - d_center|^2 * sharpness).

layout(set = 1, binding = 0) uniform sampler2D t_AORaw;
layout(set = 1, binding = 1) uniform sampler2D t_Depth;

layout(location = 0) in  vec2 v_TexCoord;
layout(location = 0) out float out_AO;

layout(push_constant) uniform PC {
    vec2  texelStep;    // (1/w, 0) for H pass, (0, 1/h) for V pass
    float sharpness;    // depth similarity sensitivity (~10.0)
    float _pad;
};

void main() {
    const float gauss[5] = float[](0.0625, 0.25, 0.375, 0.25, 0.0625);

    float centerDepth = texture(t_Depth, v_TexCoord).r;
    float aoSum   = 0.0;
    float weightSum = 0.0;

    for (int i = 0; i < 5; ++i) {
        vec2  uv    = v_TexCoord + texelStep * float(i - 2);
        float ao    = texture(t_AORaw, uv).r;
        float d     = texture(t_Depth, uv).r;
        float diff  = d - centerDepth;
        float w     = gauss[i] * exp(-diff * diff * sharpness);
        aoSum      += ao * w;
        weightSum  += w;
    }

    out_AO = aoSum / max(weightSum, 1e-5);
}
