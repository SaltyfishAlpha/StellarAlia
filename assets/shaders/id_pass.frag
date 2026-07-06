#version 450

// Writes the DrawItem id into the R32_UINT pick buffer (0 = background).
layout(location = 0) in flat uint v_id;
layout(location = 0) out uint o_id;

void main() {
    o_id = v_id;
}
