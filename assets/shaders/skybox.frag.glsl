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
    vec4 fogParams;
    vec4 shadowParams;
};

layout(push_constant) uniform Push {
    vec4 horizonColor;
    vec4 zenithColor;
    float timeOfDay;
} push;

layout(location = 0) in vec3 WorldPos;
layout(location = 1) in float Altitude;

layout(location = 0) out vec4 outColor;

void main() {
    float t = clamp(Altitude, 0.0, 1.0);
    t = pow(t, 1.5);
    vec3 sky = mix(push.horizonColor.rgb, push.zenithColor.rgb, t);
    float scatter = max(0.0, 1.0 - t * 2.0) * 0.15;
    sky += vec3(scatter * 0.8, scatter * 0.4, scatter * 0.1);
    outColor = vec4(sky, 1.0);
}
