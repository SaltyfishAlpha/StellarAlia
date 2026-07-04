#version 450
#extension GL_GOOGLE_include_directive : enable

// set=0: frame uniforms, IBL textures, LTC LUTs (via pbr.glsl → frame_uniforms.glsl)
// set=1: G-Buffer inputs (filled by G-Buffer pass)
#include "pbr.glsl"
#include "shading_models.glsl"     // encode/decode helpers, SHADING_FLAG_HAS_RT3
#include "shading_model_ids.glsl"  // generated SHADING_MODEL_* defines

// ── G-Buffer inputs (set=1) ───────────────────────────────────────────────────
//   binding=0  RT0: albedo.rgb + occlusion.a                              (RGBA8_UNORM)
//   binding=1  RT1: octahedral-encoded normal (RG) + roughness(B) + metallic(A)  (RGBA16F)
//   binding=2  RT2: data.rgb + shading_model_id.a                         (RGBA16F)
//              RT2.rgb meaning depends on shading model (emissive.rgb for PBR).
//              RT2.a   = EncodeShadingFlags(modelID [, extraFlags])
//   binding=3  depth sampler (for world-pos reconstruction)
//   binding=4  shadow map (D32, directional light)
layout(set = 2, binding = 0) uniform sampler2D t_GAlbedoOcclusion;
layout(set = 2, binding = 1) uniform sampler2D t_GNormalMaterial;
layout(set = 2, binding = 2) uniform sampler2D t_GData;
layout(set = 2, binding = 3) uniform sampler2D t_GDepth;
// Renamed from t_ShadowMap (Issue #56): frame_uniforms.glsl now declares a
// t_ShadowMap at set=1 binding=7 for forward passes. Same binding, same data.
layout(set = 2, binding = 4) uniform sampler2D t_GShadowMap;
layout(set = 2, binding = 5) uniform sampler2D t_AO;  // GTAO result (1.0 = no occlusion)

// ── I/O ───────────────────────────────────────────────────────────────────────
layout(location = 0) in  vec2 v_TexCoord;
layout(location = 0) out vec4 out_HDRColor;

// ── G-Buffer data struct ──────────────────────────────────────────────────────
// Passed to all shading model evaluators (*.lighting.glsl).
// Fields are unpacked from the G-Buffer before dispatch; their meaning may vary
// per shading model — the G-Buffer fragment shader and its evaluator must agree.
struct GBufferData {
    vec3  albedo;
    float occlusion;
    vec3  N;
    float roughness;
    float metallic;
    vec3  data;      // RT2.rgb — emissive for PBR, custom for other models
    vec3  worldPos;
};

// ── Shading model evaluators (auto-generated dispatch) ───────────────────────
// GBufferData and shading_model_ids.glsl must be in scope before this include.
// Re-run CMake to regenerate when new *.lighting.glsl files are registered.
#include "shading_dispatch.glsl"

// ── Shared PBR shading (Issue #56) — also used by forward_transparent.frag ──
#include "pbr_shading.glsl"

// ── Main ──────────────────────────────────────────────────────────────────────
void main() {
    // ── Sample G-Buffer ───────────────────────────────────────────────────────
    vec4  albedoOcc = texture(t_GAlbedoOcclusion, v_TexCoord);
    vec4  normalMat = texture(t_GNormalMaterial,  v_TexCoord);
    vec4  dataVec   = texture(t_GData,            v_TexCoord);
    float depth     = texture(t_GDepth,           v_TexCoord).r;

    // Background pixels (depth == 1.0): transparent output lets skybox show through.
    if (depth >= 1.0 - EPSILON) { out_HDRColor = vec4(0.0); return; }

    // ── Unpack into GBufferData ───────────────────────────────────────────────
    GBufferData gbuf;
    gbuf.albedo    = albedoOcc.rgb;
    gbuf.occlusion = albedoOcc.a * texture(t_AO, v_TexCoord).r;
    gbuf.N         = OctDecode(normalMat.rg);
    gbuf.roughness = normalMat.b;
    gbuf.metallic  = normalMat.a;
    gbuf.data      = dataVec.rgb;
    gbuf.worldPos  = ReconstructWorldPos(v_TexCoord, depth, u_Frame.invViewProj);

    // ── Dispatch to shading model ─────────────────────────────────────────────
    uint modelID = DecodeShadingModel(dataVec.a);

    vec3 customColor;
    if (DispatchShadingModel(modelID, gbuf, customColor)) {
        out_HDRColor = vec4(customColor, 1.0);
        return;
    }
    // DispatchShadingModel returns false for PBR (0) and unknown IDs → fall through

    // ── PBR (SHADING_MODEL_PBR = 0, and fallback for unknown IDs) ────────────
    // gbuf.data = RT2.rgb = emissive for PBR.
    vec3 color = EvaluatePBRShading(t_GShadowMap,
                                    gbuf.albedo, gbuf.occlusion, gbuf.N,
                                    gbuf.roughness, gbuf.metallic,
                                    gbuf.data, gbuf.worldPos);
    out_HDRColor = vec4(color, 1.0);
}
