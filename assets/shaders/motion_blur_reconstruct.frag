#version 450
#extension GL_GOOGLE_include_directive : enable
#include "frame_uniforms.glsl"

// Reconstruct (Issue #46): McGuire 2012 "Plausible Motion Blur".
// For each pixel, take the tile's NeighborMax velocity, march N samples along
// it in pixel space, and blend HDR colours weighted by depth comparison + cone
// (sample velocity alignment with tile-dominant velocity). Strength + maxSpeed
// applied here so the velocity buffer stays "physical".

layout(set = 2, binding = 0) uniform sampler2D t_HDR;
layout(set = 2, binding = 1) uniform sampler2D t_Velocity;
layout(set = 2, binding = 2) uniform sampler2D t_NeighborMax;
layout(set = 2, binding = 3) uniform sampler2D t_Depth;

layout(location = 0) in  vec2 v_TexCoord;
layout(location = 0) out vec4 out_Color;

layout(push_constant) uniform PC {
    float strength;       // user-facing artistic multiplier
    float maxSpeed;       // NDC clamp on velocity magnitude
    int   samples;        // [4..32]
    int   _pad;
    vec2  invScreenSize;  // 1.0 / viewport
    vec2  invTileSize;    // 1.0 / tile pixel size (= 1/16)
};

// FNV-1a-ish hash → [0, 1) jitter; decorrelates samples per pixel to break tile banding.
float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float linearDepth(float rawZ) {
    // Reuse invProj from FrameUniforms; positive value (camera in front).
    vec4 vp = u_Frame.invProj * vec4(0.0, 0.0, rawZ, 1.0);
    return -vp.z / vp.w;
}

// McGuire 2012 soft depth comparison.
// Returns 1 when z_sample is "in front" of z_center (close), 0 when far behind.
float softZCompare(float zCenter, float zSample) {
    const float kSoftExtent = 0.5;  // meters; tuned for typical scene depth scale
    return clamp(1.0 - (zSample - zCenter) / kSoftExtent, 0.0, 1.0);
}

// Cone falloff: sample velocity v_i alignment with reference offset r (in px).
float cone(float magV, float r) {
    return clamp(1.0 - r / magV, 0.0, 1.0);
}

void main() {
    vec4  centerColor = texture(t_HDR, v_TexCoord);

    vec2 tileUv = (gl_FragCoord.xy * invTileSize) * invScreenSize;
    // Account for tile texture being lower-res — fetch by tile coordinate.
    ivec2 tileSize = textureSize(t_NeighborMax, 0);
    ivec2 tileXY   = clamp(ivec2(v_TexCoord * vec2(tileSize)), ivec2(0), tileSize - ivec2(1));
    vec2  maxV     = texelFetch(t_NeighborMax, tileXY, 0).rg * strength;

    // Clamp to maxSpeed (NDC magnitude).
    float magMaxV = length(maxV);
    if (magMaxV > maxSpeed) maxV *= (maxSpeed / max(magMaxV, 1e-6));
    magMaxV = length(maxV);

    // Sub-pixel threshold: when the dominant velocity is less than half a pixel,
    // skip the blur — pure passthrough avoids burning GPU on static scenes.
    if (magMaxV * length(1.0 / invScreenSize) < 0.5) {
        out_Color = centerColor;
        return;
    }

    vec2  centerVel = texture(t_Velocity, v_TexCoord).rg * strength;
    float zCenter   = linearDepth(texture(t_Depth, v_TexCoord).r);

    float jitter = hash21(gl_FragCoord.xy) - 0.5;
    vec4  accum  = centerColor;
    float weight = 1.0;

    for (int i = 0; i < samples; ++i) {
        // t in [-0.5, 0.5], jittered to break banding
        float t  = (float(i) + jitter + 0.5) / float(samples) - 0.5;
        if (t == 0.0) continue;
        vec2  uv = v_TexCoord + maxV * t;

        // Sample HDR / velocity / depth at offset point
        vec2  v_i   = texture(t_Velocity, uv).rg * strength;
        float z_i  = linearDepth(texture(t_Depth, uv).r);
        vec4  c    = texture(t_HDR, uv);

        // Offset magnitude in pixels (NDC * resolution)
        float r_px = abs(t) * magMaxV * length(1.0 / invScreenSize);

        // McGuire weight: foreground vs background + cone over sample velocity.
        // f: 1 when sample is in front of center (i.e. tile-dominant motion belongs to sample)
        // b: 1 when sample is behind center (background bleeding into foreground)
        float f = softZCompare(zCenter, z_i);
        float b = softZCompare(z_i, zCenter);
        float magV_i = length(v_i);
        float w = f * cone(magV_i, r_px)
                + b * cone(magMaxV, r_px)
                + cone(magV_i, r_px) * cone(magMaxV, r_px) * 2.0;

        accum  += c * w;
        weight += w;
    }
    out_Color = accum / weight;
}
