#version 450

layout(set = 1, binding = 0) uniform sampler2D uTexture;

layout(push_constant) uniform Push {
    vec2 tileCount;
    int alphaKey;
} push;

layout(location = 0) in vec4 vColor;
layout(location = 1) in float vTile;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 p = gl_PointCoord;
    float tile = floor(vTile);
    float tx = mod(tile, push.tileCount.x);
    float ty = floor(tile / push.tileCount.x);
    vec2 uv = (vec2(tx, ty) + p) / push.tileCount;
    vec4 texColor = texture(uTexture, uv);

    if (push.alphaKey != 0) {
        float lum = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
        if (lum < 0.05) discard;
    }

    float edge = smoothstep(0.5, 0.4, length(p - 0.5));
    outColor = texColor * vColor * vec4(vec3(1.0), edge);
}
