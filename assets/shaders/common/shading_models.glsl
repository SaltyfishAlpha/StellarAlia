// ── Shading model G-Buffer flag bits ──────────────────────────────────────────
// RT2.a stores a float-encoded uint bitmask (safe in RGBA16F up to 255).
//
//   Bits [3:0]  Shading model ID  — defined in generated/shading_model_ids.glsl
//   Bit  [4]    Material writes optional RT3 for extra G-Buffer data
//   Bits [7:5]  Reserved
//
// Write from G-Buffer fragment shader:
//   out_GData.a = EncodeShadingFlags(SHADING_MODEL_FOO [, extraFlags]);
//
// Read in deferred lighting pass:
//   uint modelID = DecodeShadingModel(dataVec.a);
//   bool hasRT3  = HasRT3(dataVec.a);

// ── Extra flag bits (OR into model when writing RT2.a) ────────────────────────
#define SHADING_FLAG_HAS_RT3        16u  // bit 4: material wrote optional RT3

// ── Encode / Decode ───────────────────────────────────────────────────────────

float EncodeShadingFlags(uint model) {
    return float(model);
}
float EncodeShadingFlags(uint model, uint extraFlags) {
    return float(model | extraFlags);
}

uint DecodeShadingModel(float encoded) {
    return uint(round(encoded)) & 0xFu;
}

bool HasRT3(float encoded) {
    return (uint(round(encoded)) & SHADING_FLAG_HAS_RT3) != 0u;
}
