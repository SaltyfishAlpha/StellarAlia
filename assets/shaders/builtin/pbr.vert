#version 450
#extension GL_GOOGLE_include_directive : enable
#include "frame_uniforms.glsl"

// ── Vertex inputs (standard mesh layout) ─────────────────────────────────────
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec4 a_Tangent;    // w = handedness (+1 or -1)
layout(location = 3) in vec2 a_TexCoord0;

// ── Per-draw push constant (model matrix) ────────────────────────────────────
layout(push_constant) uniform PushConstants { mat4 model; } pc;

// ── Outputs to fragment stage ─────────────────────────────────────────────────
layout(location = 0) out vec3 v_WorldPos;
layout(location = 1) out vec3 v_Normal;
layout(location = 2) out vec4 v_Tangent;
layout(location = 3) out vec2 v_TexCoord0;

void main() {
    vec4 worldPos = pc.model * vec4(a_Position, 1.0);
    v_WorldPos    = worldPos.xyz;
    mat3 nm       = transpose(inverse(mat3(pc.model)));
    v_Normal      = normalize(nm * a_Normal);
    v_Tangent     = vec4(normalize(nm * a_Tangent.xyz), a_Tangent.w);
    v_TexCoord0   = a_TexCoord0;
    gl_Position   = u_Frame.viewProj * worldPos;
}
