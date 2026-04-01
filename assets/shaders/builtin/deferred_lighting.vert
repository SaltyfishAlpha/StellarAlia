#version 450

// Fullscreen quad vertex shader for deferred lighting pass

layout(location = 0) out vec2 fragTexCoord;

void main() {
    // Generate fullscreen triangle
    // No vertex buffer needed - generate positions in shader
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    
    vec2 texCoords[3] = vec2[](
        vec2(0.0, 0.0),
        vec2(2.0, 0.0),
        vec2(0.0, 2.0)
    );
    
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    fragTexCoord = texCoords[gl_VertexIndex];
}

