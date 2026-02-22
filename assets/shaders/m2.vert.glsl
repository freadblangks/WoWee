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
    vec2 uvOffset;
    int texCoordSet;
    int useBones;
} push;

layout(set = 2, binding = 0) readonly buffer BoneSSBO {
    mat4 bones[];
};

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aBoneWeights;
layout(location = 4) in vec4 aBoneIndicesF;
layout(location = 5) in vec2 aTexCoord2;

layout(location = 0) out vec3 FragPos;
layout(location = 1) out vec3 Normal;
layout(location = 2) out vec2 TexCoord;

void main() {
    vec4 pos = vec4(aPos, 1.0);
    vec4 norm = vec4(aNormal, 0.0);

    if (push.useBones != 0) {
        ivec4 bi = ivec4(aBoneIndicesF);
        mat4 skinMat = bones[bi.x] * aBoneWeights.x
                     + bones[bi.y] * aBoneWeights.y
                     + bones[bi.z] * aBoneWeights.z
                     + bones[bi.w] * aBoneWeights.w;
        pos = skinMat * pos;
        norm = skinMat * norm;
    }

    vec4 worldPos = push.model * pos;
    FragPos = worldPos.xyz;
    Normal = mat3(push.model) * norm.xyz;

    TexCoord = (push.texCoordSet == 1 ? aTexCoord2 : aTexCoord) + push.uvOffset;

    gl_Position = projection * view * worldPos;
}
