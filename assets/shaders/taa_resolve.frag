#version 450
#extension GL_GOOGLE_include_directive : enable
#include "frame_uniforms.glsl"

// set=1 inputs
layout(set = 2, binding = 0) uniform sampler2D t_Current;   // RGBA16F, current jittered HDR
layout(set = 2, binding = 1) uniform sampler2D t_History;   // RGBA16F, previous TAA result
layout(set = 2, binding = 2) uniform sampler2D t_Depth;     // D32F, for world-pos reprojection

layout(location = 0) in  vec2 v_TexCoord;
layout(location = 0) out vec4 out_Color;

layout(push_constant) uniform PC {
    float blendStatic;    // history lerp ratio toward current in still regions (e.g. 0.1)
    float blendMotion;    // history lerp ratio toward current in motion regions  (e.g. 0.5)
    float historyValid;   // 0.0 = first frame or after resize → output current unmodified
    float antiGhosting;   // 1.0 = enable 3×3 neighborhood AABB clamp; 0.0 = skip
};

// ── World-space position from depth ──────────────────────────────────────────
// The depth buffer was rendered with a jittered projection: subtract the NDC-space
// jitter offset before applying the unjittered invViewProj so world-pos is exact.
vec3 WorldPos(vec2 uv) {
    float d       = texture(t_Depth, uv).r;
    vec2  jitterNDC = 2.0 * u_Frame.jitter / u_Frame.resolution;
    vec4  ndc     = vec4(uv * 2.0 - 1.0 - jitterNDC, d, 1.0);  // Vulkan NDC Z in [0,1]
    vec4  wp      = u_Frame.invViewProj * ndc;
    return wp.xyz / wp.w;
}

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

    // ── Reproject to previous frame UV ───────────────────────────────────────
    vec3 worldPos  = WorldPos(v_TexCoord);
    vec4 prevClip  = u_Frame.prevViewProj * vec4(worldPos, 1.0);
    vec2 prevNDC   = prevClip.xy / prevClip.w;
    vec2 prevUV    = prevNDC * 0.5 + 0.5;

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

    // ── Motion-adaptive blend ─────────────────────────────────────────────────
    // velLen in [0, ~1]: 0 = perfectly still, 1 = large motion
    vec2  vel    = v_TexCoord - prevUV;
    float velLen = clamp(length(vel) * 100.0, 0.0, 1.0);
    float blend  = mix(blendStatic, blendMotion, velLen);

    out_Color = mix(historyClamped, current, blend);
}
