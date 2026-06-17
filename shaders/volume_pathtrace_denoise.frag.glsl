#version 450

// M4 v2 P2.1 -- denoise pass for path-traced volumetric rendering. Sits between
// the path-trace and display passes: reads the linear HDR running average from
// the accumulation texture and writes a (denoised) RGBA16Float intermediate.
// P2.1 is plumbing only -- this shader is intentionally a pass-through identity
// copy so we can verify the new pipeline routes correctly before the real
// A-trous wavelet filter lands in P2.2.

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform texture2D inputTex;
layout(set = 0, binding = 1) uniform sampler   inputSampler;

void main() {
    ivec2 fragCoord = ivec2(gl_FragCoord.xy);
    vec3 hdr = texelFetch(sampler2D(inputTex, inputSampler), fragCoord, 0).rgb;
    outColor = vec4(hdr, 1.0);
}
