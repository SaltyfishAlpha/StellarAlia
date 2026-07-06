// ─────────────────────────────────────────────────────────────────────────────
// set=0 global bindless texture heap — shared by custom .saglsl shading models
// (Issue #73-A). Built-in PBR declares the same heap via material_params_pbr.glsl.
//
// Usage in a .saglsl gbuffer section:
//   #extension GL_EXT_nonuniform_qualifier : enable   (after #version)
//   #include "bindless_textures.glsl"
//   ... SampleBindless(u_Mat.t_BaseColor_Idx, v_TexCoord0) ...
//
// Texture slots live inside the MaterialParams SSBO as `uint t_<Name>_Idx`
// members (index 0 = default white). MaterialManager strips the `_Idx` suffix,
// so .samat texture maps keep logical names like "t_BaseColor".
// ─────────────────────────────────────────────────────────────────────────────
#ifndef SA_BINDLESS_TEXTURES_GLSL
#define SA_BINDLESS_TEXTURES_GLSL

layout(set = 0, binding = 0) uniform sampler2D globalTex[];

#define SampleBindless(idx, uv) texture(globalTex[nonuniformEXT(idx)], uv)

#endif // SA_BINDLESS_TEXTURES_GLSL
