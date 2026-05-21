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
}   // 128 bytes — MUST match C++ ObjectData (InstancedRenderData.hpp) and
    // gbuffer.wgsl / frustum_cull.comp.wgsl. A stray extra vec4 here made the
    // stride 144, so objects[i>0] read misaligned garbage and the shadow map
    // was filled with nonsense geometry.

struct ObjectBuffer {
    objects: array<ObjectData>,
}

@group(1) @binding(0) var<storage, read> objectBuffer: ObjectBuffer;

// Vertex input (per-vertex only) — keep in sync with shared engine Vertex layout
struct VertexInput {
    @builtin(instance_index) instanceIndex: u32,
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) texCoord: vec2<f32>,
    @location(3) tangent: vec3<f32>,  // unused in shadow pass; declared to match shared layout
}

// Vertex output
struct VertexOutput {
    @builtin(position) position: vec4<f32>,
}

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;

    // Direct indexing — render ALL instances; cull the ground geometrically
    // below. Index tricks (+1u / firstInstance) proved unreliable across the
    // Vulkan↔WebGPU instance_index semantics and let the giant ground plane
    // into the shadow map (whole receiver self-shadowed).
    let obj = objectBuffer.objects[input.instanceIndex];

    // The ground is an enormous flat quad (AABB span ~100000); it must not
    // cast shadows. Detect it by its huge horizontal extent and emit a
    // clipped vertex (NDC z = -2 < 0 → discarded), so it writes nothing.
    let ext = obj.boundingBoxMax.xyz - obj.boundingBoxMin.xyz;
    if (max(ext.x, ext.z) > 10000.0) {
        output.position = vec4<f32>(0.0, 0.0, -2.0, 1.0);
        return output;
    }

    let worldPos = obj.worldMatrix * vec4<f32>(input.position, 1.0);
    output.position = ubo.lightSpaceMatrix * worldPos;
    return output;
}

// Empty fragment shader for depth-only shadow pass
@fragment
fn fs_main() {
}
