#version 450

layout(set = 1, binding = 0) uniform sampler2D markerTexture;

layout(push_constant) uniform Push {
    mat4 model;
    float alpha;
} push;

layout(location = 0) in vec2 TexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = texture(markerTexture, TexCoord);
    if (texColor.a < 0.1) discard;
    outColor = vec4(texColor.rgb, texColor.a * push.alpha);
}
