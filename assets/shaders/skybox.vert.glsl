#version 450

layout(set = 0, binding = 0) uniform PerFrame {
    mat4 view;
    mat4 projection;
    mat4 lightSpaceMatrix;
    vec4 lightDir;
    vec4 lightColor;
    vec4 ambientColor;
    vec4 viewPos;
    vec4 fogColor;
    vec4 fogParams; // x=fogStart, y=fogEnd, z=time
    vec4 shadowParams; // x=enabled, y=strength
};

layout(location = 0) in vec3 aPos;

layout(location = 0) out vec3 WorldPos;
layout(location = 1) out float Altitude;

void main() {
    WorldPos = aPos;
    Altitude = aPos.y;
    mat4 rotView = mat4(mat3(view)); // strip translation
    vec4 pos = projection * rotView * vec4(aPos, 1.0);
    gl_Position = pos.xyww; // force far plane
}
