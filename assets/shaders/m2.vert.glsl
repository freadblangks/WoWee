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
    int isFoliage;
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
layout(location = 3) flat out vec3 InstanceOrigin;
layout(location = 4) out float ModelHeight;

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

    // Wind animation for foliage
    if (push.isFoliage != 0) {
        float windTime = fogParams.z;
        vec3 worldRef = push.model[3].xyz;
        float heightFactor = clamp(pos.z / 20.0, 0.0, 1.0);
        heightFactor *= heightFactor; // quadratic — base stays grounded

        // Layer 1: Trunk sway — slow, large amplitude
        float trunkPhase = windTime * 0.8 + dot(worldRef.xy, vec2(0.1, 0.13));
        float trunkSwayX = sin(trunkPhase) * 0.35 * heightFactor;
        float trunkSwayY = cos(trunkPhase * 0.7) * 0.25 * heightFactor;

        // Layer 2: Branch sway — medium frequency, per-branch phase
        float branchPhase = windTime * 1.7 + dot(worldRef.xy, vec2(0.37, 0.71));
        float branchSwayX = sin(branchPhase + pos.y * 0.4) * 0.15 * heightFactor;
        float branchSwayY = cos(branchPhase * 1.1 + pos.x * 0.3) * 0.12 * heightFactor;

        // Layer 3: Leaf flutter — fast, small amplitude, per-vertex
        float leafPhase = windTime * 4.5 + dot(aPos, vec3(1.7, 2.3, 0.9));
        float leafFlutterX = sin(leafPhase) * 0.06 * heightFactor;
        float leafFlutterY = cos(leafPhase * 1.3) * 0.05 * heightFactor;

        pos.x += trunkSwayX + branchSwayX + leafFlutterX;
        pos.y += trunkSwayY + branchSwayY + leafFlutterY;
    }

    vec4 worldPos = push.model * pos;
    FragPos = worldPos.xyz;
    Normal = mat3(push.model) * norm.xyz;

    TexCoord = (push.texCoordSet == 1 ? aTexCoord2 : aTexCoord) + push.uvOffset;

    InstanceOrigin = push.model[3].xyz;
    ModelHeight = pos.z;

    gl_Position = projection * view * worldPos;
}
