#version 450

layout(push_constant) uniform Push {
    vec2 position;
    float size;
    float aspectRatio;
    vec4 color; // rgb + brightness in w
} push;

layout(location = 0) in vec2 UV;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 center = UV - 0.5;
    float dist = length(center);
    float alpha = smoothstep(0.5, 0.0, dist);
    float glow = exp(-dist * dist * 8.0) * 0.5;
    alpha = max(alpha, glow) * push.color.w;
    if (alpha < 0.01) discard;
    outColor = vec4(push.color.rgb, alpha);
}
