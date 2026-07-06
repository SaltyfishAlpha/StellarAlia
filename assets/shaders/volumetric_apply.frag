#version 450
#extension GL_GOOGLE_include_directive : enable

// Volumetric Fog — Apply (Issue #49, pass 3/3).
//
// Fullscreen composite: reconstruct the pixel's view depth, sample the
// integrated froxel volume (hardware trilinear = free XY upsample + Z
// interpolation) and blend energy-conservingly over the lit HDR:
//     out = hdr · T + inscatter
// Sky pixels (depth == 1) fall past fogFar and clamp to the farthest slice.
#include "frame_uniforms.glsl"
#include "volumetric_common.glsl"

layout(set = 2, binding = 0) uniform sampler2D t_HDR;
layout(set = 2, binding = 1) uniform sampler2D t_Depth;
// Named t_FogIntegrated: frame_uniforms.glsl owns the name t_FogVolume at
// set=1 binding=8 (Step 9, forward transparents); this is the same texture
// bound privately at set=2 for the fullscreen composite.
layout(set = 2, binding = 2) uniform sampler3D t_FogIntegrated;  // rgb=inscatter, a=T

layout(location = 0) in  vec2 v_UV;
layout(location = 0) out vec4 out_Color;

layout(push_constant) uniform PC {
    float fogFar;
} pc;

float NdcToViewZ(float ndcZ) {
    vec4 c = u_Frame.invProj * vec4(0.0, 0.0, ndcZ, 1.0);
    return c.z / c.w;
}

void main() {
    vec3  hdr   = texture(t_HDR, v_UV).rgb;
    float ndcZ  = texture(t_Depth, v_UV).r;
    float viewD = -NdcToViewZ(ndcZ);

    vec3 volSize = vec3(textureSize(t_FogIntegrated, 0));
    // Voxel k stores the integral up to the END of slice k → sample half a
    // voxel back. Clamp to half-texel insets: the shared sampler is repeat-
    // addressed and edge wrap would bleed far fog into slice 0 / across screen edges.
    float slice  = VolFogDepthToSlice(min(viewD, pc.fogFar), volSize.z, pc.fogFar);
    float wCoord = clamp((slice - 0.5) / volSize.z, 0.5 / volSize.z, 1.0 - 0.5 / volSize.z);
    vec2  uv     = clamp(v_UV, 0.5 / volSize.xy, 1.0 - 0.5 / volSize.xy);

    vec4 fog = texture(t_FogIntegrated, vec3(uv, wCoord));
    out_Color = vec4(hdr * fog.a + fog.rgb, 1.0);
}
