// ─────────────────────────────────────────────────────────────────────────────
// set=0 — Per-Frame Descriptors
//
// Bound ONCE per frame by the render loop before any draw calls.
// ALL engine shaders share this exact layout — never change binding indices.
// C++ mirror: src/function/FrameUniforms.hpp
// ─────────────────────────────────────────────────────────────────────────────
#ifndef SA_FRAME_UNIFORMS_GLSL
#define SA_FRAME_UNIFORMS_GLSL

// binding=0  Camera / time / screen
layout(set = 0, binding = 0) uniform FrameData {
    mat4  view;
    mat4  proj;
    mat4  viewProj;
    mat4  invViewProj;
    vec3  cameraPos;   float time;
    vec2  resolution;  float deltaTime;  float _pad0;
} u_Frame;

// binding=1  Primary directional light + ambient
layout(set = 0, binding = 1) uniform LightData {
    vec3  direction;   float intensity;
    vec3  color;       float _pad0;
    vec3  ambientColor; float ambientIntensity;
} u_Light;

#endif // SA_FRAME_UNIFORMS_GLSL
