// GPU Frustum Culling Compute Shader
// Phase 2.2: Tests each object's AABB against camera frustum planes
// Outputs visible object indices and indirect draw command

struct CullUniforms {
    frustumPlanes: array<vec4<f32>, 6>,  // (normal.xyz, distance)
    objectCount: u32,
    indexCount: u32,
    pad0: u32,
    pad1: u32,
}

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

struct IndirectDrawCommand {
    indexCount: u32,
    instanceCount: atomic<u32>,
    firstIndex: u32,
    vertexOffset: i32,
    firstInstance: u32,
}

struct VisibleIndicesBuffer {
    indices: array<u32>,
}

@group(0) @binding(0) var<uniform> cull: CullUniforms;
@group(0) @binding(1) var<storage, read> objectBuffer: ObjectBuffer;
@group(0) @binding(2) var<storage, read_write> indirect: IndirectDrawCommand;
@group(0) @binding(3) var<storage, read_write> visibleIndices: VisibleIndicesBuffer;

fn isAABBOutsidePlane(plane: vec4<f32>, bboxMin: vec3<f32>, bboxMax: vec3<f32>) -> bool {
    let pVertex = vec3<f32>(
        select(bboxMin.x, bboxMax.x, plane.x > 0.0),
        select(bboxMin.y, bboxMax.y, plane.y > 0.0),
        select(bboxMin.z, bboxMax.z, plane.z > 0.0),
    );
    return dot(plane.xyz, pVertex) + plane.w < 0.0;
}

@compute @workgroup_size(64, 1, 1)
fn main(@builtin(global_invocation_id) globalID: vec3<u32>) {
    let objectIndex = globalID.x;
    if (objectIndex >= cull.objectCount) {
        return;
    }

    let bboxMin = objectBuffer.objects[objectIndex].boundingBoxMin.xyz;
    let bboxMax = objectBuffer.objects[objectIndex].boundingBoxMax.xyz;

    var visible = true;
    for (var i = 0; i < 6; i++) {
        if (isAABBOutsidePlane(cull.frustumPlanes[i], bboxMin, bboxMax)) {
            visible = false;
            break;
        }
    }

    if (visible) {
        let slot = atomicAdd(&indirect.instanceCount, 1u);
        visibleIndices.indices[slot] = objectIndex;
    }
}
