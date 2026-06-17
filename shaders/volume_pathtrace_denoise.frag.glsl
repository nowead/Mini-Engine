#version 450

// M4 v2 P2.2 -- single-iteration A-trous wavelet denoiser. Reads the linear-HDR
// running average from the path-trace accumulation and writes a color-guided
// cross-bilateral 5x5 blur to the denoise intermediate. P2.3 will add multi-
// iteration ping-pong; here we run a single level-0 kernel (spacing=1) so the
// cost is bounded and the visual change easy to read.

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform texture2D inputTex;
layout(set = 0, binding = 1) uniform sampler   inputSampler;

float atrousWeight(int dx, int dy) {
    float w[5] = float[5](1.0/16.0, 4.0/16.0, 6.0/16.0, 4.0/16.0, 1.0/16.0);
    return w[dx + 2] * w[dy + 2];
}

void main() {
    ivec2 dims      = textureSize(sampler2D(inputTex, inputSampler), 0);
    ivec2 center    = ivec2(gl_FragCoord.xy);
    vec3 centerCol  = texelFetch(sampler2D(inputTex, inputSampler), center, 0).rgb;

    const float sigmaC2 = 0.35 * 0.35;

    // A-trous level-2 tap spacing: 25 taps cover an 8x8 px window instead of
    // the dense 5x5. Without this the single-iteration filter is sub-pixel
    // Gaussian on a 1700+ wide canvas and invisible. P2.3 will cascade levels
    // 0/1/2 (spacings 1, 2, 4) in three ping-pong iterations.
    const int stride = 4;

    vec3 accum = vec3(0.0);
    float wSum = 0.0;
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            ivec2 p = clamp(center + ivec2(dx, dy) * stride, ivec2(0), dims - ivec2(1));
            vec3  col = texelFetch(sampler2D(inputTex, inputSampler), p, 0).rgb;
            vec3  d   = col - centerCol;
            float wC  = exp(-dot(d, d) / sigmaC2);
            float wK  = atrousWeight(dx, dy);
            float w   = wK * wC;
            accum += col * w;
            wSum  += w;
        }
    }
    vec3 denoised = accum / max(wSum, 1e-6);
    outColor = vec4(denoised, 1.0);
}
