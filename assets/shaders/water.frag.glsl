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
    mat4 model;
    float waveAmp;
    float waveFreq;
    float waveSpeed;
} push;

layout(set = 1, binding = 0) uniform WaterMaterial {
    vec4 waterColor;
    float waterAlpha;
    float shimmerStrength;
    float alphaScale;
};

layout(location = 0) in vec3 FragPos;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 TexCoord;
layout(location = 3) in float WaveOffset;

layout(location = 0) out vec4 outColor;

void main() {
    float time = fogParams.z;
    vec3 norm = normalize(Normal);
    vec3 viewDir = normalize(viewPos.xyz - FragPos);
    vec3 ldir = normalize(-lightDir.xyz);

    float diff = max(dot(norm, ldir), 0.0);
    vec3 halfDir = normalize(ldir + viewDir);
    float spec = pow(max(dot(norm, halfDir), 0.0), 128.0);
    float sparkle = sin(FragPos.x * 20.0 + time * 3.0) * sin(FragPos.z * 20.0 + time * 2.5);
    sparkle = max(0.0, sparkle) * shimmerStrength;

    vec3 color = waterColor.rgb * (ambientColor.rgb + diff * lightColor.rgb)
               + spec * lightColor.rgb * 0.5
               + sparkle * lightColor.rgb * 0.3;

    float crest = smoothstep(0.3, 1.0, WaveOffset) * 0.15;
    color += vec3(crest);

    float fresnel = pow(1.0 - max(dot(norm, viewDir), 0.0), 3.0);
    float alpha = mix(waterAlpha * 0.6, waterAlpha, fresnel) * alphaScale;

    float dist = length(viewPos.xyz - FragPos);
    alpha *= smoothstep(800.0, 200.0, dist);

    float fogFactor = clamp((fogParams.y - dist) / (fogParams.y - fogParams.x), 0.0, 1.0);
    color = mix(fogColor.rgb, color, fogFactor);

    outColor = vec4(color, alpha);
}
