// Vertex shader for GPU instancing demo (WGSL for WebGPU)

struct CameraUBO {
    view: mat4x4<f32>,
    proj: mat4x4<f32>,
}

@group(0) @binding(0) var<uniform> camera: CameraUBO;

struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) texCoord: vec2<f32>,
}

struct InstanceInput {
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
fn main(
    vertex: VertexInput,
    instance: InstanceInput
) -> VertexOutput {
    var output: VertexOutput;

    // Apply instance transform
    let worldPos = vertex.position * instance.instanceScale + instance.instancePosition;
    output.position = camera.proj * camera.view * vec4<f32>(worldPos, 1.0);
    output.color = instance.instanceColor;
    output.normal = vertex.normal;

    return output;
}
