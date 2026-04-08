#version 450

// Bloom Threshold Compute Shader
// Reads HDR color texture, extracts pixels above luminance threshold,
// writes half-resolution result to bloom storage image.
//
// Workgroup: 8x8 threads
// Dispatch:  ceil(width/16) x ceil(height/16)  (2x downscale)

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 0, binding = 0) uniform texture2D  hdrInput;   // RGBA16Float HDR scene
layout(set = 0, binding = 1) uniform sampler    linearSampler;
layout(set = 0, binding = 2, rgba16f) uniform image2D bloomOutput;  // half-res RGBA16Float

layout(push_constant) uniform BloomThresholdParams {
    vec2  invInputSize;   // 1.0 / full-res size
    float threshold;      // luminance cutoff (default ~1.0 in HDR)
    float knee;           // soft-knee width (default 0.5)
} params;

// Perceptual luminance
float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// Soft-knee threshold curve: smoothly ramps above `threshold`
vec3 quadraticThreshold(vec3 color, float threshold, float knee) {
    float lum = luminance(color);
    float rq   = clamp(lum - threshold + knee, 0.0, 2.0 * knee);
    rq = (rq * rq) / (4.0 * knee + 0.00001);
    float weight = max(rq, lum - threshold) / max(lum, 0.00001);
    return color * weight;
}

void main() {
    ivec2 outCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 outSize  = imageSize(bloomOutput);

    if (outCoord.x >= outSize.x || outCoord.y >= outSize.y) return;

    // Sample full-res HDR at 2x downscale (4-tap average for quality)
    vec2 uv = (vec2(outCoord) + 0.5) * 2.0 * params.invInputSize;
    vec2 o  = params.invInputSize;

    vec3 s0 = texture(sampler2D(hdrInput, linearSampler), uv + vec2(-o.x, -o.y)).rgb;
    vec3 s1 = texture(sampler2D(hdrInput, linearSampler), uv + vec2( o.x, -o.y)).rgb;
    vec3 s2 = texture(sampler2D(hdrInput, linearSampler), uv + vec2(-o.x,  o.y)).rgb;
    vec3 s3 = texture(sampler2D(hdrInput, linearSampler), uv + vec2( o.x,  o.y)).rgb;
    vec3 avg = (s0 + s1 + s2 + s3) * 0.25;

    vec3 bright = quadraticThreshold(avg, params.threshold, params.knee);
    imageStore(bloomOutput, outCoord, vec4(bright, 1.0));
}
