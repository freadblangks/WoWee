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

layout(set = 1, binding = 0) uniform sampler2D uTexture;

layout(set = 1, binding = 1) uniform WMOMaterial {
    int hasTexture;
    int alphaTest;
    int unlit;
    int isInterior;
    float specularIntensity;
    int isWindow;
};

layout(set = 0, binding = 1) uniform sampler2DShadow uShadowMap;

layout(location = 0) in vec3 FragPos;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 TexCoord;
layout(location = 3) in vec4 VertColor;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = hasTexture != 0 ? texture(uTexture, TexCoord) : vec4(1.0);
    if (alphaTest != 0 && texColor.a < 0.5) discard;

    vec3 norm = normalize(Normal);
    if (!gl_FrontFacing) norm = -norm;

    vec3 result;

    if (unlit != 0) {
        result = texColor.rgb;
    } else if (isInterior != 0) {
        vec3 mocv = max(VertColor.rgb, vec3(0.5));
        result = texColor.rgb * mocv;
    } else {
        vec3 ldir = normalize(-lightDir.xyz);
        float diff = max(dot(norm, ldir), 0.0);

        vec3 viewDir = normalize(viewPos.xyz - FragPos);
        vec3 halfDir = normalize(ldir + viewDir);
        float spec = pow(max(dot(norm, halfDir), 0.0), 32.0) * specularIntensity;

        float shadow = 1.0;
        if (shadowParams.x > 0.5) {
            vec4 lsPos = lightSpaceMatrix * vec4(FragPos, 1.0);
            vec3 proj = lsPos.xyz / lsPos.w;
            proj.xy = proj.xy * 0.5 + 0.5;
            if (proj.x >= 0.0 && proj.x <= 1.0 &&
                proj.y >= 0.0 && proj.y <= 1.0 &&
                proj.z >= 0.0 && proj.z <= 1.0) {
                float bias = max(0.0005 * (1.0 - dot(norm, ldir)), 0.00005);
                shadow = texture(uShadowMap, vec3(proj.xy, proj.z - bias));
            }
            shadow = mix(1.0, shadow, shadowParams.y);
        }

        result = ambientColor.rgb * texColor.rgb
               + shadow * (diff * lightColor.rgb * texColor.rgb + spec * lightColor.rgb);

        result *= max(VertColor.rgb, vec3(0.5));
    }

    float dist = length(viewPos.xyz - FragPos);
    float fogFactor = clamp((fogParams.y - dist) / (fogParams.y - fogParams.x), 0.0, 1.0);
    result = mix(fogColor.rgb, result, fogFactor);

    float alpha = texColor.a;

    // Window glass: opaque but simulates dark tinted glass with reflections.
    // No real alpha blending — we darken the base texture and add reflection
    // on top so it reads as glass without needing the transparent pipeline.
    if (isWindow != 0) {
        vec3 viewDir = normalize(viewPos.xyz - FragPos);
        float NdotV = abs(dot(norm, viewDir));
        // Fresnel: strong reflection at grazing angles
        float fresnel = 0.08 + 0.92 * pow(1.0 - NdotV, 4.0);

        // Glass darkness depends on view angle — bright when sun glints off,
        // darker when looking straight on with no sun reflection.
        vec3 ldir = normalize(-lightDir.xyz);
        vec3 reflectDir = reflect(-viewDir, norm);
        float sunGlint = pow(max(dot(reflectDir, ldir), 0.0), 32.0);

        // Base ranges from dark (0.3) to bright (0.9) based on sun reflection
        float baseBrightness = mix(0.3, 0.9, sunGlint);
        vec3 glass = result * baseBrightness;

        // Reflection: blend sky/ambient color based on Fresnel
        vec3 reflectTint = mix(ambientColor.rgb * 1.2, vec3(0.6, 0.75, 1.0), 0.6);
        glass = mix(glass, reflectTint, fresnel * 0.8);

        // Sharp sun glint on glass
        vec3 halfDir = normalize(ldir + viewDir);
        float spec = pow(max(dot(norm, halfDir), 0.0), 256.0);
        glass += spec * lightColor.rgb * 0.8;

        // Broad warm sheen when sun is nearby
        float specBroad = pow(max(dot(norm, halfDir), 0.0), 12.0);
        glass += specBroad * lightColor.rgb * 0.12;

        result = glass;
        // Fresnel-based transparency: more transparent at oblique angles
        alpha = mix(0.4, 0.95, NdotV);
    }

    outColor = vec4(result, alpha);
}
