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

layout(set = 2, binding = 0) uniform sampler2D SceneColor;
layout(set = 2, binding = 1) uniform sampler2D SceneDepth;

layout(location = 0) in vec3 FragPos;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 TexCoord;
layout(location = 3) in float WaveOffset;
layout(location = 4) in vec2 ScreenUV;

layout(location = 0) out vec4 outColor;

vec3 dualScrollWaveNormal(vec2 p, float time) {
    // Two independently scrolling octaves (normal-map style layering).
    vec2 d1 = normalize(vec2(0.86, 0.51));
    vec2 d2 = normalize(vec2(-0.47, 0.88));
    float f1 = 0.19;
    float f2 = 0.43;
    float s1 = 0.95;
    float s2 = 1.73;
    float a1 = 0.26;
    float a2 = 0.12;

    vec2 p1 = p + d1 * (time * s1 * 4.0);
    vec2 p2 = p + d2 * (time * s2 * 4.0);

    float ph1 = dot(p1, d1) * f1;
    float ph2 = dot(p2, d2) * f2;

    float c1 = cos(ph1);
    float c2 = cos(ph2);

    float dHx = c1 * d1.x * f1 * a1 + c2 * d2.x * f2 * a2;
    float dHz = c1 * d1.y * f1 * a1 + c2 * d2.y * f2 * a2;

    return normalize(vec3(-dHx, 1.0, -dHz));
}

void main() {
    float time = fogParams.z;
    vec3 meshNorm = normalize(Normal);
    vec3 waveNorm = dualScrollWaveNormal(FragPos.xz, time);
    vec3 norm = normalize(mix(meshNorm, waveNorm, 0.82));

    vec3 viewDir = normalize(viewPos.xyz - FragPos);
    vec3 ldir = normalize(-lightDir.xyz);
    float ndotv = max(dot(norm, viewDir), 0.0);

    float diff = max(dot(norm, ldir), 0.0);
    vec3 halfDir = normalize(ldir + viewDir);
    float spec = pow(max(dot(norm, halfDir), 0.0), 96.0);
    float sparkle = sin(FragPos.x * 20.0 + time * 3.0) * sin(FragPos.z * 20.0 + time * 2.5);
    sparkle = max(0.0, sparkle) * shimmerStrength;

    float crest = smoothstep(0.3, 1.0, WaveOffset) * 0.15;
    float dist = length(viewPos.xyz - FragPos);

    // Beer-Lambert style approximation from view distance.
    float opticalDepth = 1.0 - exp(-dist * 0.0035);
    vec3 litTransmission = waterColor.rgb * (ambientColor.rgb * 0.85 + diff * lightColor.rgb * 0.55);
    vec3 absorbed = mix(litTransmission, waterColor.rgb * 0.52, opticalDepth);
    absorbed += vec3(crest);

    // Schlick Fresnel with water-like F0.
    const float F0 = 0.02;
    float fresnel = F0 + (1.0 - F0) * pow(1.0 - ndotv, 5.0);
    vec2 refractOffset = norm.xz * (0.012 + 0.02 * fresnel);
    vec2 refractUV = clamp(ScreenUV + refractOffset, vec2(0.001), vec2(0.999));
    vec3 sceneRefract = texture(SceneColor, refractUV).rgb;

    float sceneDepth = texture(SceneDepth, refractUV).r;
    float waterDepth = clamp((sceneDepth - gl_FragCoord.z) * 180.0, 0.0, 1.0);
    float depthBlend = waterDepth;
    // Fallback when sampled depth does not provide meaningful separation.
    if (sceneDepth <= gl_FragCoord.z + 1e-4) {
        depthBlend = 0.45 + opticalDepth * 0.40;
    }
    depthBlend = clamp(depthBlend, 0.28, 1.0);
    vec3 refractedTint = mix(sceneRefract, absorbed, depthBlend);

    vec3 specular = spec * lightColor.rgb * (0.45 + 0.75 * fresnel)
                  + sparkle * lightColor.rgb * 0.30;
    // Add a clear surface reflection lobe at grazing angles.
    vec3 envReflect = mix(fogColor.rgb, lightColor.rgb, 0.38) * vec3(0.75, 0.86, 1.0);
    vec3 reflection = envReflect * (0.45 + 0.55 * fresnel) + specular;
    float reflectWeight = clamp(fresnel * 1.15, 0.0, 0.92);
    vec3 color = mix(refractedTint, reflection, reflectWeight);

    float alpha = mix(waterAlpha * 1.05, min(1.0, waterAlpha * 1.30), fresnel) * alphaScale;
    alpha *= smoothstep(1600.0, 350.0, dist);
    alpha = clamp(alpha, 0.50, 1.0);

    float fogFactor = clamp((fogParams.y - dist) / (fogParams.y - fogParams.x), 0.0, 1.0);
    color = mix(fogColor.rgb, color, fogFactor);

    outColor = vec4(color, alpha);
}
