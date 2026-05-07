#version 450

// Writes 1.0 to the R8 selection mask for every visible fragment of the
// selected entity. Depth test against existing scene depth is handled by
// pipeline state (depthTest=true, depthWrite=false).
layout(location = 0) out float o_mask;

void main() {
    o_mask = 1.0;
}
