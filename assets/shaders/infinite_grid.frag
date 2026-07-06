#version 450
#include "frame_uniforms.glsl"

layout(location = 0) in  vec3 v_nearPoint;
layout(location = 1) in  vec3 v_farPoint;
layout(location = 0) out vec4 o_color;

// Anti-aliased grid alpha for an infinite XZ grid at the given spacing.
// Uses fwidth to keep 1-pixel-wide lines at any zoom level.
float gridAlpha(vec2 xz, float spacing) {
    vec2  coord   = xz / spacing;
    vec2  wrapped = abs(fract(coord - 0.5) - 0.5);
    vec2  d       = fwidth(coord);
    vec2  line    = wrapped / max(d, vec2(1e-4));
    return 1.0 - clamp(min(line.x, line.y), 0.0, 1.0);
}

void main() {
    // ── Ray / plane intersection (Y = 0) ─────────────────────────────────────
    float denom = v_farPoint.y - v_nearPoint.y;
    if (abs(denom) < 1e-6) discard;
    float t = -v_nearPoint.y / denom;
    if (t <= 0.0) discard;

    vec3 pos = v_nearPoint + t * (v_farPoint - v_nearPoint);

    // ── Distance fade (smoothstep avoids hard cutoff) ─────────────────────────
    float camDist = length(pos.xz - u_Frame.cameraPos.xz);
    float fade    = 1.0 - smoothstep(80.0, 200.0, camDist);
    if (fade <= 0.0) discard;

    // ── Fine grid (1 m) and coarse grid (10 m) ────────────────────────────────
    float a1  = gridAlpha(pos.xz, 1.0);
    float a10 = gridAlpha(pos.xz, 10.0);

    // Coarse lines are brighter; fine lines are subtler.
    vec3  gridColor = vec3(0.30);
    float gridAlpha = max(a1 * 0.55, a10 * 0.90);

    // ── Axis highlights ───────────────────────────────────────────────────────
    // X axis (the line along world-X where Z == 0) → red
    float xAxisA = 1.0 - clamp(abs(pos.z) / max(fwidth(pos.z), 1e-4), 0.0, 1.0);
    // Z axis (the line along world-Z where X == 0) → blue
    float zAxisA = 1.0 - clamp(abs(pos.x) / max(fwidth(pos.x), 1e-4), 0.0, 1.0);

    if (xAxisA > 0.0) gridColor = mix(gridColor, vec3(0.85, 0.20, 0.20), xAxisA);
    if (zAxisA > 0.0) gridColor = mix(gridColor, vec3(0.20, 0.20, 0.85), zAxisA);
    gridAlpha = max(gridAlpha, max(xAxisA, zAxisA));

    float alpha = gridAlpha * fade;
    if (alpha < 0.005) discard;

    // ── Write correct clip depth for geometry occlusion ───────────────────────
    // Unjittered (Issue #107): the grid draws after TAA; a jittered depth makes
    // the occlusion boundary against geometry wobble per frame. The ray itself
    // already comes from the unjittered invViewProj.
    vec4 clipPos = u_Frame.currUnjitteredViewProj * vec4(pos, 1.0);
    gl_FragDepth = clipPos.z / clipPos.w;

    o_color = vec4(gridColor, alpha);
}
