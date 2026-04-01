#version 450

layout(location = 0) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

void main() {
    // Simply output the interpolated color
    // This will create a gradient from red to green to blue
    outColor = vec4(fragColor, 1.0);
}

