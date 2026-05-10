#version 450
#extension GL_GOOGLE_include_directive : enable
#include "frame_uniforms.glsl"

layout(set = 1, binding = 0) uniform sampler2D t_Depth;

layout(location = 0) in  vec2 v_TexCoord;
layout(location = 0) out float out_CoC;

layout(push_constant) uniform PC {
    float focusDist;    // view-space focus distance (meters)
    float aperture;     // f-number
    float focalLength;  // mm
    float maxCocPx;     // pixel radius clamp
};

void main() {
    float rawDepth = texture(t_Depth, v_TexCoord).r;

    // Reconstruct view-space depth (positive = in front of camera)
    vec4 vp = u_Frame.invProj * vec4(v_TexCoord * 2.0 - 1.0, rawDepth, 1.0);
    float linearZ = -vp.z / vp.w;

    // Thin-lens optical CoC formula
    float F  = focalLength * 0.001;           // mm → m
    float S  = max(focusDist, F * 1.001);    // guard: S > F
    float lz = max(linearZ, 0.001);
    float coc = (F * F / aperture) * (lz - S) / (lz * (S - F));

    // Scale to pixel radius (24 mm full-frame sensor height)
    out_CoC = clamp(coc / 0.024 * u_Frame.resolution.y, -maxCocPx, maxCocPx);
}
