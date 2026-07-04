// Issue #56 — fullscreen copy: taaResolved → forward-transparent composite
// target. Transparents must NOT blend into taaResolved in place (it is the
// TAA ping-pong history-write texture; next frame reads it as history).
#version 450

layout(set = 2, binding = 0) uniform sampler2D t_Source;

layout(location = 0) in  vec2 v_UV;
layout(location = 0) out vec4 out_Color;

void main() {
    out_Color = texture(t_Source, v_UV);
}
