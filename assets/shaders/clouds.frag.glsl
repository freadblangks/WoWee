#version 450

layout(push_constant) uniform Push {
    vec4 cloudColor;      // xyz = DBC-derived base cloud color, w = unused
    vec4 sunDirDensity;   // xyz = sun direction, w = density
    vec4 windAndLight;    // x = windOffset, y = sunIntensity, z = ambient, w = unused
} push;

layout(location = 0) in vec3 vWorldDir;

layout(location = 0) out vec4 outColor;

// --- Gradient noise (smoother than hash-based) ---
vec2 hash2(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)),
             dot(p, vec2(269.5, 183.3)));
    return fract(sin(p) * 43758.5453);
}

float gradientNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);

    // Quintic interpolation for smoother results
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

    float a = dot(hash2(i + vec2(0.0, 0.0)) * 2.0 - 1.0, f - vec2(0.0, 0.0));
    float b = dot(hash2(i + vec2(1.0, 0.0)) * 2.0 - 1.0, f - vec2(1.0, 0.0));
    float c = dot(hash2(i + vec2(0.0, 1.0)) * 2.0 - 1.0, f - vec2(0.0, 1.0));
    float d = dot(hash2(i + vec2(1.0, 1.0)) * 2.0 - 1.0, f - vec2(1.0, 1.0));

    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y) * 0.5 + 0.5;
}

float fbm(vec2 p) {
    float val = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 6; i++) {
        val += amp * gradientNoise(p);
        p *= 2.0;
        amp *= 0.5;
    }
    return val;
}

void main() {
    vec3 dir = normalize(vWorldDir);
    float altitude = dir.z;
    if (altitude < 0.0) discard;

    vec3 sunDir = push.sunDirDensity.xyz;
    float density = push.sunDirDensity.w;
    float windOffset = push.windAndLight.x;
    float sunIntensity = push.windAndLight.y;
    float ambient = push.windAndLight.z;

    vec2 uv = dir.xy / (altitude + 0.001);
    uv += windOffset;

    // --- 6-octave FBM for cloud shape ---
    float cloud1 = fbm(uv * 0.8);
    float cloud2 = fbm(uv * 1.6 + 5.0);
    float cloud = cloud1 * 0.7 + cloud2 * 0.3;

    // Coverage control: base coverage with detail erosion
    float baseCoverage = smoothstep(0.30, 0.55, cloud);
    float detailErosion = gradientNoise(uv * 4.0);
    cloud = baseCoverage * smoothstep(0.2, 0.5, detailErosion);
    cloud *= density;

    // Horizon fade
    float horizonFade = smoothstep(0.0, 0.15, altitude);
    cloud *= horizonFade;

    if (cloud < 0.01) discard;

    // --- Sun lighting on clouds ---
    // Sun dot product for view-relative brightness
    float sunDot = max(dot(vec3(0.0, 0.0, 1.0), sunDir), 0.0);

    // Self-shadowing: sample noise offset toward sun direction, darken if occluded
    float lightSample = fbm((uv + sunDir.xy * 0.05) * 0.8);
    float shadow = smoothstep(0.3, 0.7, lightSample);

    // Base lit color: mix dark (shadow) and bright (sunlit) based on shadow and sun
    vec3 baseColor = push.cloudColor.rgb;
    vec3 shadowColor = baseColor * (ambient * 0.8);
    vec3 litColor = baseColor * (ambient + sunIntensity * 0.6);
    vec3 cloudRgb = mix(shadowColor, litColor, shadow * sunDot);

    // Add ambient fill so clouds aren't too dark
    cloudRgb = mix(baseColor * ambient, cloudRgb, 0.7 + 0.3 * sunIntensity);

    // --- Silver lining effect at cloud edges ---
    float edgeLight = smoothstep(0.0, 0.3, cloud) * (1.0 - smoothstep(0.3, 0.8, cloud));
    cloudRgb += vec3(1.0, 0.95, 0.9) * edgeLight * sunDot * sunIntensity * 0.4;

    // --- Edge softness for alpha ---
    float edgeSoftness = smoothstep(0.0, 0.3, cloud);
    float alpha = cloud * edgeSoftness;

    if (alpha < 0.01) discard;
    outColor = vec4(cloudRgb, alpha);
}
