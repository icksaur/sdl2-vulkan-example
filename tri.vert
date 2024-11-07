#version 450
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;  

layout(location = 1) out vec2 uv;

layout(std140, binding = 0) uniform matrixBuffer {
    layout(offset=0) mat4 viewProjection;
};

void main() {
    uv = inUV;
    gl_Position = viewProjection * vec4(inPos, 0.0, 1.0);
}