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
    vec4 horizonColor;
    vec4 zenithColor;
    float timeOfDay;
} push;

layout(location = 0) in vec2 TexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    // Reconstruct world-space ray direction from screen position.
    // TexCoord is [0,1]^2; convert to NDC [-1,1]^2.
    float ndcX =  TexCoord.x * 2.0 - 1.0;
    float ndcY = -(TexCoord.y * 2.0 - 1.0);  // flip Y: Vulkan NDC Y-down, but projection already flipped

    // Unproject to view space using focal lengths from projection matrix.
    // projection[0][0] = 2*near/(right-left) = 1/tan(fovX/2)
    // projection[1][1] = 2*near/(top-bottom)  (already negated for Vulkan Y-flip)
    // We want the original magnitude, so take abs to get the focal length.
    vec3 viewDir = vec3(ndcX / projection[0][0],
                        ndcY / abs(projection[1][1]),
                        -1.0);

    // Rotate to world space: view = R*T, so R^-1 = R^T = transpose(mat3(view))
    mat3 invViewRot = transpose(mat3(view));
    vec3 worldDir = normalize(invViewRot * viewDir);

    // worldDir.z = sin(elevation); +1 = zenith, 0 = horizon, -1 = nadir
    float t = clamp(worldDir.z, 0.0, 1.0);
    t = pow(t, 1.5);
    vec3 sky = mix(push.horizonColor.rgb, push.zenithColor.rgb, t);
    float scatter = max(0.0, 1.0 - t * 2.0) * 0.15;
    sky += vec3(scatter * 0.8, scatter * 0.4, scatter * 0.1);
    outColor = vec4(sky, 1.0);
}
