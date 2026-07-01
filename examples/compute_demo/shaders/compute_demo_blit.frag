#version 450

// Fullscreen blit of the compute output texture.
//
// Samples at set=1 (the ShaderProgram "frame" slot, which this demo repurposes
// for its single source-texture layout via ShaderProgram::Desc::frameLayout).
// set=0 is reserved by ShaderProgram for the bindless heap (Issue #72), so a
// plain sampler2D must NOT live at set=0 or the pipeline layout won't match.
layout(set = 1, binding = 0) uniform sampler2D t_Source;

layout(location = 0) in  vec2 v_UV;
layout(location = 0) out vec4 out_Color;

void main() {
    out_Color = texture(t_Source, v_UV);
}
