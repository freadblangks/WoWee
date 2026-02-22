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

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexCoord;

layout(location = 0) out vec3 FragPos;
layout(location = 1) out vec3 Normal;
layout(location = 2) out vec2 TexCoord;
layout(location = 3) out float WaveOffset;
layout(location = 4) out vec2 ScreenUV;

float hashGrid(vec2 p) {
    return fract(sin(dot(floor(p), vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
    float time = fogParams.z;
    vec4 worldPos = push.model * vec4(aPos, 1.0);
    float px = worldPos.x;
    float py = worldPos.z;
    float dist = length(worldPos.xyz - viewPos.xyz);
    float blend = smoothstep(150.0, 400.0, dist);

    float seamless = sin(px * push.waveFreq + time * push.waveSpeed) * 0.6
                   + sin(py * push.waveFreq * 0.7 + time * push.waveSpeed * 1.3) * 0.3
                   + sin((px + py) * push.waveFreq * 0.5 + time * push.waveSpeed * 0.7) * 0.1;

    float gridWave = sin(px * push.waveFreq + time * push.waveSpeed + hashGrid(vec2(px, py) * 0.01) * 6.28) * 0.5
                   + sin(py * push.waveFreq * 0.8 + time * push.waveSpeed * 1.1 + hashGrid(vec2(py, px) * 0.01) * 6.28) * 0.5;

    float wave = mix(seamless, gridWave, blend);
    worldPos.y += wave * push.waveAmp;
    WaveOffset = wave;

    float dx = cos(px * push.waveFreq + time * push.waveSpeed) * push.waveFreq * push.waveAmp;
    float dz = cos(py * push.waveFreq * 0.7 + time * push.waveSpeed * 1.3) * push.waveFreq * 0.7 * push.waveAmp;
    Normal = normalize(vec3(-dx, 1.0, -dz));

    FragPos = worldPos.xyz;
    TexCoord = aTexCoord;
    vec4 clipPos = projection * view * worldPos;
    gl_Position = clipPos;
    vec2 ndc = clipPos.xy / max(clipPos.w, 1e-5);
    ScreenUV = ndc * 0.5 + 0.5;
}
