#version 450

layout(location = 0) out vec2 TexCoord;

void main() {
    // Fullscreen triangle trick: 3 vertices, no vertex buffer
    TexCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(TexCoord * 2.0 - 1.0, 0.0, 1.0);
    TexCoord.y = 1.0 - TexCoord.y; // flip Y for Vulkan
}
