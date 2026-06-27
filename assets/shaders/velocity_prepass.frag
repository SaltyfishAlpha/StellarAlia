#version 450

// Shared velocity-prepass fragment shader (Issue #84).
// Computes per-pixel screen-space velocity = (currUV − prevUV) using clip-space
// positions interpolated by rasterization. Works for both static and skinned
// vertex shader variants; depth test enabled in pipeline so only the closest
// surface writes.

layout(location = 0) in vec4 v_CurrClip;
layout(location = 1) in vec4 v_PrevClip;

layout(location = 0) out vec2 out_Velocity;

void main() {
    if (v_PrevClip.w <= 0.0 || v_CurrClip.w <= 0.0) {
        out_Velocity = vec2(0.0);
        return;
    }

    vec2 currNDC = v_CurrClip.xy / v_CurrClip.w;
    vec2 prevNDC = v_PrevClip.xy / v_PrevClip.w;

    vec2 currUV = currNDC * 0.5 + 0.5;
    vec2 prevUV = prevNDC * 0.5 + 0.5;

    out_Velocity = currUV - prevUV;
}
