#version 450

// GPU Frustum Culling Compute Shader
// Phase 2.2: Tests each object's AABB against camera frustum planes
// Outputs visible object indices and indirect draw command

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// Frustum culling uniforms
layout(std140, set = 0, binding = 0) uniform CullUniforms {
    vec4 frustumPlanes[6];   // (normal.xyz, distance) — Left, Right, Bottom, Top, Near, Far
    uint objectCount;
    uint indexCount;          // Mesh index count for indirect draw command
    uint pad0;
    uint pad1;
} cull;

// Per-object data (read-only, same struct as building shader)
struct ObjectData {
    mat4 worldMatrix;
    vec4 boundingBoxMin;
    vec4 boundingBoxMax;
    vec4 colorAndMetallic;
    vec4 roughnessAOPad;
};

layout(std430, set = 0, binding = 1) readonly buffer ObjectBuffer {
    ObjectData objects[];
};

// Indirect draw command (read/write — atomicAdd on instanceCount)
layout(std430, set = 0, binding = 2) buffer IndirectBuffer {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
} indirect;

// Visible object indices (write-only output)
layout(std430, set = 0, binding = 3) writeonly buffer VisibleIndicesBuffer {
    uint visibleIndices[];
};

// Test if AABB is completely behind a frustum plane
// Returns true if the AABB is outside (should be culled)
bool isAABBOutsidePlane(vec4 plane, vec3 bboxMin, vec3 bboxMax) {
    // Find the p-vertex: the point on AABB most aligned with plane normal
    vec3 pVertex = vec3(
        plane.x > 0.0 ? bboxMax.x : bboxMin.x,
        plane.y > 0.0 ? bboxMax.y : bboxMin.y,
        plane.z > 0.0 ? bboxMax.z : bboxMin.z
    );
    // If p-vertex is behind the plane, entire AABB is outside
    return dot(plane.xyz, pVertex) + plane.w < 0.0;
}

void main() {
    uint objectIndex = gl_GlobalInvocationID.x;
    if (objectIndex >= cull.objectCount) return;

    // Read world-space AABB
    vec3 bboxMin = objects[objectIndex].boundingBoxMin.xyz;
    vec3 bboxMax = objects[objectIndex].boundingBoxMax.xyz;

    // Test against all 6 frustum planes
    bool visible = true;
    for (int i = 0; i < 6; i++) {
        if (isAABBOutsidePlane(cull.frustumPlanes[i], bboxMin, bboxMax)) {
            visible = false;
            break;
        }
    }

    if (visible) {
        uint slot = atomicAdd(indirect.instanceCount, 1);
        visibleIndices[slot] = objectIndex;
    }
}
