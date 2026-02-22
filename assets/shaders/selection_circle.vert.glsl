#version 450

layout(push_constant) uniform Push {
    mat4 mvp;
} push;

layout(location = 0) in vec3 aPos;

layout(location = 0) out vec2 vLocalPos;

void main() {
    vLocalPos = aPos.xz;
    gl_Position = push.mvp * vec4(aPos, 1.0);
}
