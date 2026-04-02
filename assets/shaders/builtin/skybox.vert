#version 450
#extension GL_GOOGLE_include_directive : enable
#include "frame_uniforms.glsl"

layout(location = 0) out vec3 v_SkyDir;

void main() {
    // Full-screen triangle (3 vertices, no vertex buffer needed)
    vec2 uv   = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec2 clip = uv * 2.0 - 1.0;

    // Reconstruct world-space sky direction via invViewProj at the far plane.
    vec4 worldPos = u_Frame.invViewProj * vec4(clip, 1.0, 1.0);
    worldPos /= worldPos.w;
    v_SkyDir = worldPos.xyz - u_Frame.cameraPos;

    // Place skybox at depth = 1.0 (far plane) so geometry always draws on top.
    gl_Position = vec4(clip, 1.0, 1.0);
}
