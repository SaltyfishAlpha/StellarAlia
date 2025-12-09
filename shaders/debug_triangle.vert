#version 450

layout(location = 0) out vec3 fragColor;

void main() {
    // Fullscreen triangle (no vertex buffer needed)
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),  // Bottom left
        vec2( 3.0, -1.0),  // Bottom right (extended)
        vec2(-1.0,  3.0)   // Top left (extended)
    );
    
    vec3 colors[3] = vec3[](
        vec3(1.0, 0.0, 0.0),  // Red
        vec3(0.0, 1.0, 0.0),  // Green
        vec3(0.0, 0.0, 1.0)   // Blue
    );
    
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    fragColor = colors[gl_VertexIndex];
}

