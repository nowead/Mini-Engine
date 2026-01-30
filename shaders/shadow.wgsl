// Shadow pass shader - renders scene from light's perspective
// WebGPU WGSL version

// Light space matrix uniform
struct LightSpaceUBO {
    lightSpaceMatrix: mat4x4<f32>,
}

@group(0) @binding(0) var<uniform> ubo: LightSpaceUBO;

// Vertex input
struct VertexInput {
    // Per-vertex attributes (binding 0)
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,      // Unused in shadow pass
    @location(2) texCoord: vec2<f32>,    // Unused in shadow pass
    // Per-instance attributes (binding 1)
    @location(3) instancePosition: vec3<f32>,
    @location(4) instanceColor: vec3<f32>,  // Unused in shadow pass
    @location(5) instanceScale: vec3<f32>,
}

// Vertex output
struct VertexOutput {
    @builtin(position) position: vec4<f32>,
}

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;

    // Apply instance transform (same as building shader)
    let worldPos = input.position * input.instanceScale + input.instancePosition;

    // Transform to light clip space
    output.position = ubo.lightSpaceMatrix * vec4<f32>(worldPos, 1.0);

    return output;
}

// Empty fragment shader for depth-only shadow pass
// Outputs nothing - only depth is written
@fragment
fn fs_main() {
    // No output needed - depth buffer is written automatically
}
