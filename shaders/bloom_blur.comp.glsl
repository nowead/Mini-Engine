#version 450

// Bloom Blur Compute Shader — Dual Kawase Blur
// Reads bloomInput (sampler), writes blurred result to bloomOutput (image).
// Run multiple passes (alternating read/write buffers) for wider blur.
//
// Workgroup: 8x8 threads

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 0, binding = 0) uniform texture2D  bloomInput;
layout(set = 0, binding = 1) uniform sampler    linearSampler;
layout(set = 0, binding = 2, rgba16f) uniform image2D bloomOutput;

layout(push_constant) uniform BloomBlurParams {
    vec2  invInputSize;   // 1.0 / size of bloomInput
    int   iteration;      // Kawase kernel offset (0, 1, 2, …)
    float pad;
} params;

void main() {
    ivec2 coord   = ivec2(gl_GlobalInvocationID.xy);
    ivec2 outSize = imageSize(bloomOutput);
    if (coord.x >= outSize.x || coord.y >= outSize.y) return;

    // Dual Kawase kernel:
    //   sample at center and 4 half-pixel-offset diagonals
    float offset = float(params.iteration) + 0.5;
    vec2  uv     = (vec2(coord) + 0.5) * params.invInputSize;
    vec2  o      = params.invInputSize * offset;

    vec3 sum = texture(sampler2D(bloomInput, linearSampler), uv).rgb * 4.0
             + texture(sampler2D(bloomInput, linearSampler), uv + vec2(-o.x, -o.y)).rgb
             + texture(sampler2D(bloomInput, linearSampler), uv + vec2( o.x, -o.y)).rgb
             + texture(sampler2D(bloomInput, linearSampler), uv + vec2(-o.x,  o.y)).rgb
             + texture(sampler2D(bloomInput, linearSampler), uv + vec2( o.x,  o.y)).rgb;

    imageStore(bloomOutput, coord, vec4(sum / 8.0, 1.0));
}
