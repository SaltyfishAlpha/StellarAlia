#version 450

#include "frame_uniforms.glsl"

layout(set = 2, binding = 0) uniform sampler2D t_LDR;

layout(push_constant) uniform PushConstants {
    float vignetteEnable;
    float vignetteIntensity;
    float vignetteSmoothness;
    float aspectRatio;
    float caEnable;
    float caStrength;
    float caPxScale;
    float _pad0;
    float filmGrainEnable;
    float filmGrainIntensity;
    float filmGrainSize;
    float _pad1;
} pc;

layout(location = 0) in  vec2 v_UV;
layout(location = 0) out vec4 out_Color;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    vec3 rgb;

    if (pc.caEnable > 0.5) {
        vec2 dir = v_UV - 0.5;
        float r = texture(t_LDR, v_UV + dir * pc.caStrength * pc.caPxScale).r;
        float g = texture(t_LDR, v_UV).g;
        float b = texture(t_LDR, v_UV - dir * pc.caStrength * pc.caPxScale).b;
        rgb = vec3(r, g, b);
    } else {
        rgb = texture(t_LDR, v_UV).rgb;
    }

    if (pc.vignetteEnable > 0.5) {
        vec2 d = (v_UV - 0.5) * vec2(pc.aspectRatio, 1.0);
        float falloff = smoothstep(pc.vignetteIntensity,
                                   pc.vignetteIntensity + pc.vignetteSmoothness,
                                   length(d));
        rgb *= 1.0 - falloff;
    }

    if (pc.filmGrainEnable > 0.5) {
        float n  = hash12(v_UV * pc.filmGrainSize * u_Frame.resolution + u_Frame.time) * 2.0 - 1.0;
        float lum = dot(rgb, vec3(0.299, 0.587, 0.114));
        rgb += n * pc.filmGrainIntensity * mix(1.0, 0.3, lum);
    }

    out_Color = vec4(rgb, 1.0);
}
