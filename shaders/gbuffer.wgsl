// G-Buffer geometry pass — WebGPU WGSL version
// Translates gbuffer.vert.glsl + gbuffer_nobindless.frag.glsl
// MRT output:
//   target 0 (RGBA16Float): normal.xyz + roughness
//   target 1 (RGBA8Unorm) : albedo_linear.rgb + metallic
//   target 2 (RGBA8Unorm) : ao (r only)
//
// Bindless is not available in WebGPU — procedural albedo from ObjectData only.

// =============================================================================
// Bind Group 0: per-frame uniforms (shared with all passes)
// =============================================================================
// Only the first three mat4s (model/view/proj) are needed for the G-Buffer pass.
// The buffer is larger on the C++ side (full UniformBufferObject) but WebGPU
// allows binding a smaller struct to a larger buffer.
struct GBufferUBO {
    model: mat4x4<f32>,  // offset   0
    view:  mat4x4<f32>,  // offset  64
    proj:  mat4x4<f32>,  // offset 128
}

@group(0) @binding(0) var<uniform> ubo: GBufferUBO;

// =============================================================================
// Bind Group 1: per-object SSBO (shared with shadow + deferred passes)
// =============================================================================
// Must match C++ ObjectData (InstancedRenderData.hpp) — 128 bytes:
//   worldMatrix      64 bytes
//   boundingBoxMin   16 bytes
//   boundingBoxMax   16 bytes
//   colorAndMetallic 16 bytes  (rgb = albedo, a = metallic)
//   roughnessAOPad   16 bytes  (r = roughness, g = ao, b = bindless idx float, a = pad)
struct ObjectData {
    worldMatrix:      mat4x4<f32>,
    boundingBoxMin:   vec4<f32>,
    boundingBoxMax:   vec4<f32>,
    colorAndMetallic: vec4<f32>,
    roughnessAOPad:   vec4<f32>,
}

struct ObjectBuffer {
    objects: array<ObjectData>,
}

struct VisibleIndicesBuffer {
    indices: array<u32>,
}

@group(1) @binding(0) var<storage, read> objectBuffer: ObjectBuffer;
@group(1) @binding(1) var<storage, read> visibleIndices: VisibleIndicesBuffer;

// =============================================================================
// Vertex I/O
// =============================================================================
struct VertexInput {
    @builtin(instance_index) instanceIndex: u32,
    @location(0) position: vec3<f32>,
    @location(1) normal:   vec3<f32>,
    @location(2) texCoord: vec2<f32>,
    @location(3) tangent:  vec3<f32>,  // consumed by normal-map fragment path in a later sub-task
}

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) worldPos:  vec3<f32>,
    @location(1) normal:    vec3<f32>,
    @location(2) albedo:    vec3<f32>,
    @location(3) metallic:  f32,
    @location(4) roughness: f32,
    @location(5) ao:        f32,
}

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    let actualIndex = visibleIndices.indices[input.instanceIndex];
    let obj = objectBuffer.objects[actualIndex];

    let worldPos4 = obj.worldMatrix * vec4<f32>(input.position, 1.0);

    // Normal matrix: upper-left 3×3 of worldMatrix.
    // Valid for uniform-scale transforms; non-uniform scale would require
    // transpose(inverse(mat3)), which WGSL does not provide natively.
    let normalMat = mat3x3<f32>(
        obj.worldMatrix[0].xyz,
        obj.worldMatrix[1].xyz,
        obj.worldMatrix[2].xyz,
    );

    var out: VertexOutput;
    out.position  = ubo.proj * ubo.view * ubo.model * worldPos4;
    out.worldPos  = worldPos4.xyz;
    out.normal    = normalize(normalMat * input.normal);
    out.albedo    = obj.colorAndMetallic.rgb;
    out.metallic  = obj.colorAndMetallic.a;
    out.roughness = obj.roughnessAOPad.r;
    out.ao        = obj.roughnessAOPad.g;
    return out;
}

// =============================================================================
// Fragment: write geometry to 3 MRT targets
// =============================================================================
struct GBufferOutput {
    @location(0) gBuffer0: vec4<f32>,  // normal.xyz + roughness
    @location(1) gBuffer1: vec4<f32>,  // albedo_linear.rgb + metallic
    @location(2) gBuffer2: vec4<f32>,  // ao (r)
}

@fragment
fn fs_main(input: VertexOutput) -> GBufferOutput {
    let N = normalize(input.normal);

    // sRGB → linear (same as gbuffer_nobindless.frag.glsl)
    let albedoLinear = pow(input.albedo, vec3<f32>(2.2));

    var out: GBufferOutput;
    out.gBuffer0 = vec4<f32>(N,              input.roughness);
    out.gBuffer1 = vec4<f32>(albedoLinear,   input.metallic);
    out.gBuffer2 = vec4<f32>(input.ao, 0.0, 0.0, 1.0);
    return out;
}
