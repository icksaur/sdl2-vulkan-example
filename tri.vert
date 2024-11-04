#version 450
layout(location = 0) in vec2 inPos;

layout(std140, binding = 0) uniform matrixBuffer {
    layout(offset=0) mat4 viewProjection;
};

void main() {
    vec4 vertex = vec4(inPos, 0.0, 1.0);
    gl_Position = viewProjection * vertex;
}