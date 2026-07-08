#version 450

layout(location = 0) in  vec3 v_Normal;
layout(location = 1) in  vec4 v_Color;
layout(location = 0) out vec4 out_Color;

void main() {
    // Two-sided half-lambert: the octahedron is drawn without back-face culling
    // (a private depth buffer resolves self-occlusion), so shading must not care
    // which way the flat normal points — abs() keeps both faces consistently lit.
    vec3  N = normalize(v_Normal);
    vec3  L = normalize(vec3(0.4, 0.85, 0.55));
    float d = abs(dot(N, L));
    float shade = d * 0.55 + 0.45;   // [0.45, 1.0] — solid look, never fully black
    out_Color = vec4(v_Color.rgb * shade, v_Color.a);
}
