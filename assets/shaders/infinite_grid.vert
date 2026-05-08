#version 450
#include "frame_uniforms.glsl"

layout(location = 0) out vec3 v_nearPoint;
layout(location = 1) out vec3 v_farPoint;

const vec2 kNDC[3] = vec2[3](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

vec3 unproject(float x, float y, float z) {
    vec4 p = u_Frame.invViewProj * vec4(x, y, z, 1.0);
    return p.xyz / p.w;
}

void main() {
    vec2 ndc   = kNDC[gl_VertexIndex];
    v_nearPoint = unproject(ndc.x, ndc.y, 0.0);
    v_farPoint  = unproject(ndc.x, ndc.y, 1.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
