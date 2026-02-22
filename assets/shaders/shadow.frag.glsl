#version 450

layout(set = 1, binding = 0) uniform sampler2D uTexture;

layout(set = 1, binding = 1) uniform ShadowParams {
    int useBones;
    int useTexture;
    int alphaTest;
    int foliageSway;
    float windTime;
    float foliageMotionDamp;
};

layout(location = 0) in vec2 TexCoord;
layout(location = 1) in vec3 WorldPos;

void main() {
    if (useTexture != 0) {
        vec2 uv = TexCoord;
        if (foliageSway != 0) {
            float sway = sin(windTime + WorldPos.x * 0.5) * 0.02 * foliageMotionDamp;
            uv += vec2(sway, sway * 0.5);
        }
        vec4 texColor = textureLod(uTexture, uv, 0.0);
        if (alphaTest != 0 && texColor.a < 0.5) discard;

        if (foliageSway != 0) {
            vec2 uv2 = TexCoord + vec2(
                sin(windTime * 1.3 + WorldPos.z * 0.7) * 0.015 * foliageMotionDamp,
                sin(windTime * 0.9 + WorldPos.x * 0.6) * 0.01 * foliageMotionDamp
            );
            vec4 texColor2 = textureLod(uTexture, uv2, 0.0);
            float blended = (texColor.a + texColor2.a) * 0.5;
            if (alphaTest != 0 && blended < 0.5) discard;
        }
    }
}
