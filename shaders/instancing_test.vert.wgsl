// Instancing test vertex shader
// WebGPU WGSL version

struct CameraUBO {
    view: mat4x4<f32>,
    proj: mat4x4<f32>,
}

@group(0) @binding(0) var<uniform> camera: CameraUBO;

struct VertexInput {
    // Per-vertex attributes (binding 0)
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) texCoord: vec2<f32>,
    // Per-instance attributes (binding 1)
    @location(3) instancePosition: vec3<f32>,
    @location(4) instanceColor: vec3<f32>,
    @location(5) instanceScale: f32,
}

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec3<f32>,
    @location(1) normal: vec3<f32>,
}

@vertex
fn main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;

    // Apply instance transform
    let worldPos = input.position * input.instanceScale + input.instancePosition;

    // Transform to clip space
    output.position = camera.proj * camera.view * vec4<f32>(worldPos, 1.0);

    // Pass color and normal to fragment shader
    output.color = input.instanceColor;
    output.normal = input.normal;

    return output;
}
