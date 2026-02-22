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
} push;

layout(set = 2, binding = 0) readonly buffer BoneSSBO {
    mat4 bones[];
};

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aBoneWeights;
layout(location = 2) in ivec4 aBoneIndices;
layout(location = 3) in vec3 aNormal;
layout(location = 4) in vec2 aTexCoord;

layout(location = 0) out vec3 FragPos;
layout(location = 1) out vec3 Normal;
layout(location = 2) out vec2 TexCoord;

void main() {
    mat4 skinMat = bones[aBoneIndices.x] * aBoneWeights.x
                 + bones[aBoneIndices.y] * aBoneWeights.y
                 + bones[aBoneIndices.z] * aBoneWeights.z
                 + bones[aBoneIndices.w] * aBoneWeights.w;

    vec4 skinnedPos = skinMat * vec4(aPos, 1.0);
    vec3 skinnedNorm = mat3(skinMat) * aNormal;

    vec4 worldPos = push.model * skinnedPos;
    FragPos = worldPos.xyz;
    Normal = mat3(push.model) * skinnedNorm;
    TexCoord = aTexCoord;

    gl_Position = projection * view * worldPos;
}
