#version 450

layout(push_constant) uniform Push {
    mat4 model;
    vec4 celestialColor; // xyz = color, w = unused
    float intensity;
    float moonPhase;
    float animTime;
} push;

layout(location = 0) in vec2 TexCoord;

layout(location = 0) out vec4 outColor;

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = fract(sin(dot(i, vec2(127.1, 311.7))) * 43758.5453);
    float b = fract(sin(dot(i + vec2(1.0, 0.0), vec2(127.1, 311.7))) * 43758.5453);
    float c = fract(sin(dot(i + vec2(0.0, 1.0), vec2(127.1, 311.7))) * 43758.5453);
    float d = fract(sin(dot(i + vec2(1.0, 1.0), vec2(127.1, 311.7))) * 43758.5453);
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main() {
    vec2 uv = TexCoord - 0.5;
    float dist = length(uv);

    // Hard circular cutoff — nothing beyond radius 0.35
    if (dist > 0.35) discard;

    // Hard disc with smooth edge
    float disc = smoothstep(0.35, 0.28, dist);

    // Soft glow confined within cutoff radius
    float glow = exp(-dist * dist * 40.0) * 0.5;

    // Combine disc and glow
    float alpha = max(disc, glow) * push.intensity;

    // Smooth fade to zero at cutoff boundary
    float edgeFade = 1.0 - smoothstep(0.25, 0.35, dist);
    alpha *= edgeFade;

    vec3 color = push.celestialColor.rgb;

    // Animated haze/turbulence overlay for the sun disc
    if (push.intensity > 0.5) {
        float noise  = valueNoise(uv * 8.0  + vec2(push.animTime * 0.3,  push.animTime * 0.2));
        float noise2 = valueNoise(uv * 16.0 - vec2(push.animTime * 0.5,  push.animTime * 0.15));
        float turbulence = (noise * 0.6 + noise2 * 0.4) * disc;
        color += vec3(turbulence * 0.3, turbulence * 0.15, 0.0);
    }

    // Moon phase shadow (only applied when intensity < 0.5, i.e. for moons)
    float phaseX = uv.x * 2.0 + push.moonPhase;
    float phaseShadow = smoothstep(-0.1, 0.1, phaseX);
    alpha *= mix(phaseShadow, 1.0, step(0.5, push.intensity));

    if (alpha < 0.01) discard;
    // Pre-multiply for additive blending: RGB is the light contribution
    outColor = vec4(color * alpha, alpha);
}
