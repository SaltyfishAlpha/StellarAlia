#version 450
#extension GL_GOOGLE_include_directive : enable
#include "frame_uniforms.glsl"

// set=2 inputs
layout(set = 2, binding = 0) uniform sampler2D t_Current;   // RGBA16F, current jittered HDR
layout(set = 2, binding = 1) uniform sampler2D t_History;   // RGBA16F, previous TAA result
layout(set = 2, binding = 2) uniform sampler2D t_Depth;     // D32F, unused after Issue #85 but kept for layout stability
layout(set = 2, binding = 3) uniform sampler2D t_Velocity;  // RG16F, unjittered per-pixel velocity (Issue #85)
layout(set = 2, binding = 4) uniform sampler2D t_Reactive;  // R8, transparent coverage (Issue #105; hdr-filler + weight 0 when absent)

layout(location = 0) in  vec2 v_TexCoord;
layout(location = 0) out vec4 out_Color;

layout(push_constant) uniform PC {
    float blendStatic;    // history lerp ratio toward current in still regions (e.g. 0.1)
    float blendMotion;    // history lerp ratio toward current in motion regions  (e.g. 0.5)
    float historyValid;   // 0.0 = first frame or after resize → output current unmodified
    float antiGhosting;   // 1.0 = enable 3×3 neighborhood AABB clamp; 0.0 = skip
    float blendReactive;  // blend floor at full transparent coverage (Issue #105); 0 = disabled
};

// ── 3×3 YCoCg neighborhood AABB ──────────────────────────────────────────────
// Clipping history in YCoCg reduces luminance-chroma cross-contamination.
vec3 RGBToYCoCg(vec3 c) {
    return vec3(
         0.25 * c.r + 0.5 * c.g + 0.25 * c.b,
        -0.25 * c.r + 0.5 * c.g - 0.25 * c.b,
         0.5  * c.r              - 0.5  * c.b);
}
vec3 YCoCgToRGB(vec3 c) {
    return clamp(vec3(
        c.x - c.y + c.z,
        c.x + c.y,
        c.x - c.y - c.z), 0.0, 65504.0);  // clamp to RGBA16F max
}

void main() {
    vec4 current = texture(t_Current, v_TexCoord);

    // ── First frame: output current directly ─────────────────────────────────
    if (historyValid < 0.5) {
        out_Color = current;
        return;
    }

    // ── Reproject via per-pixel velocity (Issue #85) ─────────────────────────
    // VelocityPrepass writes unjittered (currUV - prevUV); for static geometry
    // this still encodes camera motion via VP delta, for dynamic / skinned it
    // additionally captures per-vertex motion. Replaces the depth-based
    // WorldPos+prevViewProj path that only saw camera velocity.
    vec2 vel    = texture(t_Velocity, v_TexCoord).rg;
    vec2 prevUV = v_TexCoord - vel;

    // If the previous UV is outside the viewport the pixel is newly revealed — use current.
    if (any(lessThan(prevUV, vec2(0.0))) || any(greaterThan(prevUV, vec2(1.0)))) {
        out_Color = current;
        return;
    }

    vec4 history = texture(t_History, prevUV);

    // ── 3×3 neighborhood AABB clamp (anti-ghosting) ──────────────────────────
    vec4 historyClamped = history;
    if (antiGhosting > 0.5) {
        vec3 ycocgMin = vec3( 1e30);
        vec3 ycocgMax = vec3(-1e30);
        vec2 texelSize = 1.0 / vec2(textureSize(t_Current, 0));
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                vec3 s = RGBToYCoCg(texture(t_Current, v_TexCoord + vec2(dx, dy) * texelSize).rgb);
                ycocgMin = min(ycocgMin, s);
                ycocgMax = max(ycocgMax, s);
            }
        }
        vec3 histYCoCg = clamp(RGBToYCoCg(history.rgb), ycocgMin, ycocgMax);
        historyClamped = vec4(YCoCgToRGB(histYCoCg), history.a);
    }

    // ── Motion-adaptive blend (reuse vel from above) ─────────────────────────
    // velLen in [0, ~1]: 0 = perfectly still, 1 = large motion
    float velLen = clamp(length(vel) * 100.0, 0.0, 1.0);
    float blend  = mix(blendStatic, blendMotion, velLen);

    // Issue #105: transparents write no velocity, so their history reprojects
    // by the background's motion and ghosts. Raise the blend floor by their
    // coverage instead of discarding history outright — static transparents
    // still converge jitter AA.
    blend = max(blend, texture(t_Reactive, v_TexCoord).r * blendReactive);

    out_Color = mix(historyClamped, current, blend);
}
