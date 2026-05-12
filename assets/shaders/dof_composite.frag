#version 450

layout(set = 2, binding = 0) uniform sampler2D t_HDR;
layout(set = 2, binding = 1) uniform sampler2D t_CoC;
layout(set = 2, binding = 2) uniform sampler2D t_DofNear;
layout(set = 2, binding = 3) uniform sampler2D t_DofFar;

layout(location = 0) in  vec2 v_TexCoord;
layout(location = 0) out vec4 out_Color;

layout(push_constant) uniform PC {
    float maxCocPx;
    float nearTransition; // CoC magnitude where near blend starts
    float farTransition;  // CoC magnitude where far blend starts
    float _pad;
};

void main() {
    float coc = texture(t_CoC, v_TexCoord).r;

    float nearAlpha = smoothstep(-nearTransition, -maxCocPx, coc);
    float farAlpha  = smoothstep( farTransition,   maxCocPx, coc);

    vec4 sharp    = texture(t_HDR,     v_TexCoord);
    vec4 blurNear = texture(t_DofNear, v_TexCoord);
    vec4 blurFar  = texture(t_DofFar,  v_TexCoord);

    out_Color = mix(mix(sharp, blurFar, farAlpha), blurNear, nearAlpha);
}
