#version 450
#extension GL_GOOGLE_include_directive : enable
#include "common.glsl"
#include "shading_models.glsl"     // assets/shaders/common/
#include "shading_model_ids.glsl"  // generated/shading_model_ids.glsl

layout(set = 1, binding = 0) uniform MaterialParams {
    vec4 baseColorFactor;
} u_Mat;

layout(set = 1, binding = 1) uniform sampler2D t_BaseColor;

// Locations match deferred_geometry.vert outputs (shared vert shader).
// v_WorldPos(0) and v_Tangent(2) are unused by this material.
layout(location = 1) in vec3 v_Normal;
layout(location = 3) in vec2 v_TexCoord0;

// G-Buffer outputs
//   RT0: albedo.rgb + occlusion.a
//   RT1: oct-normal(RG) + roughness(B) + metallic(A)
//   RT2: data.rgb + shading_model_id.a  ← SHADING_MODEL_SIMPLE_ALBEDO
layout(location = 0) out vec4 out_GAlbedoOcclusion;
layout(location = 1) out vec4 out_GNormalRoughness;
layout(location = 2) out vec4 out_GData;

void main() {
    vec4 color = texture(t_BaseColor, v_TexCoord0) * u_Mat.baseColorFactor;
    vec3 N     = normalize(v_Normal);

    out_GAlbedoOcclusion = vec4(color.rgb, 1.0);
    out_GNormalRoughness  = vec4(OctEncode(N), 1.0, 0.0);
    out_GData             = vec4(0.0, 0.0, 0.0, EncodeShadingFlags(SHADING_MODEL_SIMPLE_ALBEDO));
}
