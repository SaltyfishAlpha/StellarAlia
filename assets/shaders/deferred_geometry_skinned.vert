#version 450
#extension GL_GOOGLE_include_directive : enable
#include "frame_uniforms.glsl"
#include "skin_deform.glsl"

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec4 a_Tangent;
layout(location = 3) in vec2 a_TexCoord0;

layout(push_constant) uniform PushConstants { mat4 model; } pc;

layout(location = 0) out vec3 v_WorldPos;
layout(location = 1) out vec3 v_Normal;
layout(location = 2) out vec4 v_Tangent;
layout(location = 3) out vec2 v_TexCoord0;

void main() {
    mat4 skinMat  = SkinMatrix();
    mat3 skinMat3 = mat3(skinMat);

    vec4 skinnedPos  = skinMat * vec4(a_Position, 1.0);
    vec3 skinnedNorm = normalize(skinMat3 * a_Normal);
    vec4 skinnedTang = vec4(normalize(skinMat3 * a_Tangent.xyz), a_Tangent.w);

    vec4 worldPos = pc.model * skinnedPos;
    v_WorldPos    = worldPos.xyz;
    mat3 nm       = transpose(inverse(mat3(pc.model)));
    v_Normal      = normalize(nm * skinnedNorm);
    v_Tangent     = vec4(normalize(nm * skinnedTang.xyz), skinnedTang.w);
    v_TexCoord0   = a_TexCoord0;
    gl_Position   = u_Frame.viewProj * worldPos;
}
