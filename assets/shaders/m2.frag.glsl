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

layout(set = 1, binding = 2) uniform M2Material {
    int hasTexture;
    int alphaTest;
    int colorKeyBlack;
    float colorKeyThreshold;
    int unlit;
    int blendMode;
    float fadeAlpha;
    float interiorDarken;
    float specularIntensity;
};

layout(set = 0, binding = 1) uniform sampler2DShadow uShadowMap;

layout(location = 0) in vec3 FragPos;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 TexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = hasTexture != 0 ? texture(uTexture, TexCoord) : vec4(1.0);

    float alphaCutoff = 0.5;
    if (alphaTest == 2) {
        // Vegetation cutout: lower threshold to preserve leaf coverage at grazing angles.
        alphaCutoff = 0.33;
    } else if (alphaTest == 3) {
        // Ground detail clutter (grass/small cards) needs softer clipping.
        alphaCutoff = 0.20;
    } else if (alphaTest != 0) {
        alphaCutoff = 0.35;
    }
    if (alphaTest == 2) {
        float alpha = texColor.a;
        float softBand = 0.12;
        if (alpha < (alphaCutoff - softBand)) discard;
        if (alpha < alphaCutoff) {
            vec2 p = floor(gl_FragCoord.xy);
            float n = fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
            float keep = clamp((alpha - (alphaCutoff - softBand)) / softBand, 0.0, 1.0);
            if (n > keep) discard;
        }
    } else if (alphaTest != 0 && texColor.a < alphaCutoff) {
        discard;
    }
    if (colorKeyBlack != 0) {
        float lum = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
        if (lum < colorKeyThreshold) discard;
    }
    if (blendMode == 1 && texColor.a < 0.004) discard;

    vec3 norm = normalize(Normal);
    bool foliageTwoSided = (alphaTest == 2);
    if (!foliageTwoSided && !gl_FrontFacing) norm = -norm;

    vec3 ldir = normalize(-lightDir.xyz);
        float nDotL = dot(norm, ldir);
        float diff = foliageTwoSided ? abs(nDotL) : max(nDotL, 0.0);

    vec3 result;
    if (unlit != 0) {
        result = texColor.rgb;
    } else {
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
                float bias = max(0.0005 * (1.0 - abs(dot(norm, ldir))), 0.00005);
                shadow = texture(uShadowMap, vec3(proj.xy, proj.z - bias));
            }
            shadow = mix(1.0, shadow, shadowParams.y);
            if (foliageTwoSided) shadow = max(shadow, 0.45);
        }

        result = ambientColor.rgb * texColor.rgb
               + shadow * (diff * lightColor.rgb * texColor.rgb + spec * lightColor.rgb);

        if (interiorDarken > 0.0) {
            result *= mix(1.0, 0.5, interiorDarken);
        }
    }

    float dist = length(viewPos.xyz - FragPos);
    float fogFactor = clamp((fogParams.y - dist) / (fogParams.y - fogParams.x), 0.0, 1.0);
    result = mix(fogColor.rgb, result, fogFactor);

    float outAlpha = texColor.a * fadeAlpha;
    // Cutout materials should not remain partially transparent after discard,
    // otherwise foliage cards look view-dependent.
    if (alphaTest != 0 || colorKeyBlack != 0) {
        outAlpha = fadeAlpha;
    }
    // Foliage cutout should stay opaque after alpha discard to avoid
    // view-angle translucency artifacts.
    if (alphaTest == 2 || alphaTest == 3) {
        outAlpha = 1.0 * fadeAlpha;
    }
    outColor = vec4(result, outAlpha);
}
