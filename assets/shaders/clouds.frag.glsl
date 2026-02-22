#version 450

layout(push_constant) uniform Push {
    vec4 cloudColor;
    float density;
    float windOffset;
} push;

layout(location = 0) in vec3 vWorldDir;

layout(location = 0) out vec4 outColor;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p) {
    float val = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 4; i++) {
        val += amp * noise(p);
        p *= 2.0;
        amp *= 0.5;
    }
    return val;
}

void main() {
    vec3 dir = normalize(vWorldDir);
    float altitude = dir.z;   // Z is up in the Z-up world coordinate system
    if (altitude < 0.0) discard;

    vec2 uv = dir.xy / (altitude + 0.001);  // XY is the horizontal plane
    uv += push.windOffset;

    float cloud1 = fbm(uv * 0.8);
    float cloud2 = fbm(uv * 1.6 + 5.0);
    float cloud = cloud1 * 0.7 + cloud2 * 0.3;
    cloud = smoothstep(0.35, 0.65, cloud) * push.density;

    float edgeBreak = noise(uv * 4.0);
    cloud *= smoothstep(0.2, 0.5, edgeBreak);

    float horizonFade = smoothstep(0.0, 0.15, altitude);
    cloud *= horizonFade;

    float edgeSoftness = smoothstep(0.0, 0.3, cloud);
    float alpha = cloud * edgeSoftness;

    if (alpha < 0.01) discard;
    outColor = vec4(push.cloudColor.rgb, alpha);
}
