#version 450

layout(push_constant) uniform Push {
    mat4 lightSpaceMatrix;
    mat4 model;
} push;

layout(set = 2, binding = 0) readonly buffer BoneSSBO {
    mat4 bones[];
};

layout(set = 1, binding = 1) uniform ShadowParams {
    int useBones;
    int useTexture;
    int alphaTest;
    int foliageSway;
    float windTime;
    float foliageMotionDamp;
};

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aBoneWeights;
layout(location = 3) in vec4 aBoneIndicesF;

layout(location = 0) out vec2 TexCoord;
layout(location = 1) out vec3 WorldPos;

void main() {
    vec4 pos = vec4(aPos, 1.0);

    if (useBones != 0) {
        ivec4 bi = ivec4(aBoneIndicesF);
        mat4 skinMat = bones[bi.x] * aBoneWeights.x
                     + bones[bi.y] * aBoneWeights.y
                     + bones[bi.z] * aBoneWeights.z
                     + bones[bi.w] * aBoneWeights.w;
        pos = skinMat * pos;
    }

    vec4 worldPos = push.model * pos;
    WorldPos = worldPos.xyz;
    TexCoord = aTexCoord;
    gl_Position = push.lightSpaceMatrix * worldPos;
}
