#version 450

// Issue #83 X-2: alpha-composite the opaque skeleton offscreen target over the
// scene. The sampler lives in the material set (set = 2) so it matches the
// engine's BindTexture(descSet, binding) + SetDescriptorSet(2, ...) convention.
// The target's alpha channel carries the skeleton opacity (from the instance
// color) — the AlphaBlend pipeline uses it as the blend factor.
layout(set = 2, binding = 0) uniform sampler2D t_Skeleton;

layout(location = 0) in  vec2 v_UV;
layout(location = 0) out vec4 out_Color;

void main() {
    out_Color = texture(t_Skeleton, v_UV);
}
