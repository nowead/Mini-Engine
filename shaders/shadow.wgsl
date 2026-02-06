// Shadow pass shader - renders scene from light's perspective
// WebGPU WGSL version
// Phase 2.1: Uses SSBO for per-object data

// Light space matrix uniform
struct LightSpaceUBO {
    lightSpaceMatrix: mat4x4<f32>,
}

@group(0) @binding(0) var<uniform> ubo: LightSpaceUBO;

// Phase 2.1: Per-object data via SSBO
struct ObjectData {
    worldMatrix: mat4x4<f32>,
    boundingBoxMin: vec4<f32>,
    boundingBoxMax: vec4<f32>,
    colorAndMetallic: vec4<f32>,
    roughnessAOPad: vec4<f32>,
}

struct ObjectBuffer {
    objects: array<ObjectData>,
}

@group(1) @binding(0) var<storage, read> objectBuffer: ObjectBuffer;

// Vertex input (per-vertex only)
struct VertexInput {
    @builtin(instance_index) instanceIndex: u32,
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) texCoord: vec2<f32>,
}

// Vertex output
struct VertexOutput {
    @builtin(position) position: vec4<f32>,
}

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;

    let obj = objectBuffer.objects[input.instanceIndex];
    let worldPos = obj.worldMatrix * vec4<f32>(input.position, 1.0);

    output.position = ubo.lightSpaceMatrix * worldPos;

    return output;
}

// Empty fragment shader for depth-only shadow pass
@fragment
fn fs_main() {
}
