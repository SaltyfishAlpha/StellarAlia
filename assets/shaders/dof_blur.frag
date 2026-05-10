#version 450
#extension GL_GOOGLE_include_directive : enable
#include "frame_uniforms.glsl"

layout(set = 1, binding = 0) uniform sampler2D t_Src;
layout(set = 1, binding = 1) uniform sampler2D t_CoC;

layout(location = 0) in  vec2 v_TexCoord;
layout(location = 0) out vec4 out_Color;

layout(push_constant) uniform PC {
    int   isHorizontal; // 1 = H pass, 0 = V pass
    int   isNear;       // 1 = near blur (coc < 0), 0 = far blur (coc > 0)
    float maxCocPx;
    int   samples;      // total kernel width; actual half-radius = min(|coc|, samples/2)
};

void main() {
    float centerCoc = texture(t_CoC, v_TexCoord).r;

    // cocAbs > 0 when this pixel is in the relevant blur zone
    float cocAbs = isNear == 1 ? max(0.0, -centerCoc) : max(0.0, centerCoc);

    out_Color = texture(t_Src, v_TexCoord);
    if (cocAbs < 0.5) return;

    vec2  texelSize = 1.0 / u_Frame.resolution;
    vec2  dir       = isHorizontal == 1 ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    int   halfR     = min(int(cocAbs), max(1, samples / 2));
    float sigma2    = float(halfR * halfR + 1);

    vec4  colorSum  = vec4(0.0);
    float weightSum = 0.0;

    for (int i = -halfR; i <= halfR; ++i) {
        vec2  uv = v_TexCoord + dir * float(i) * texelSize;
        float g  = exp(-float(i * i) / sigma2);
        colorSum  += texture(t_Src, uv) * g;
        weightSum += g;
    }

    out_Color = colorSum / weightSum;
}
