// SimpleAlbedo shading model evaluator.
// Convention: every *.lighting.glsl must implement exactly:
//   vec3 EvaluateShading(GBufferData gbuf)
// The generated shading_dispatch.glsl renames this via #define before including.
//
// G-Buffer contract (written by simple_albedo.gbuffer.frag):
//   RT0.rgb = albedo colour
//   RT2.a   = EncodeShadingFlags(SHADING_MODEL_SIMPLE_ALBEDO)

vec3 EvaluateShading(GBufferData gbuf) {
    return gbuf.albedo;
}
