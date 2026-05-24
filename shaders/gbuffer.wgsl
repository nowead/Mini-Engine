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
// MUST match C++ ObjectData (InstancedRenderData.hpp) — 144 bytes. See that
// file for the field contract.
struct ObjectData {
    worldMatrix:      mat4x4<f32>,
    boundingBoxMin:   vec4<f32>,
    boundingBoxMax:   vec4<f32>,
    colorAndMetallic: vec4<f32>,   // rgb = albedo, a = metallic
    roughnessAOPad:   vec4<f32>,   // r = roughness, g = ao, b = legacy bindless albedo idx, a = pad
    textureIndices:   vec4<u32>,   // x = baseColor, y = normal, z = MR, w = emissive (0xFFFFFFFF = none)
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
// Bind Group 2: PBR material textures + sampler (WebGPU only)
// =============================================================================
// Buildings receive a default bind group whose 1×1 dummies are the identity
// values for the multiply-by-factor PBR pipeline: white baseColor, flat
// (0,0,1) normal, MR=(*,1,1,*), black emissive, AO=(1,*,*,*). The showcase
// asset rebinds set 2 with its own glTF textures before its draw call.
// Step 6a wires baseColor, 6b adds normal+TBN, 6c finishes MR + occlusion
// here. Emissive sampling waits for a 4th G-Buffer attachment (step 6d).
@group(2) @binding(0) var baseColorTex: texture_2d<f32>;
@group(2) @binding(1) var normalTex:    texture_2d<f32>;
@group(2) @binding(2) var mrTex:        texture_2d<f32>;
@group(2) @binding(3) var emissiveTex:  texture_2d<f32>;
@group(2) @binding(4) var aoTex:        texture_2d<f32>;
@group(2) @binding(5) var materialSampler: sampler;

// Step 9: per-frame A/B material toggle state. abSplitX = 0 means off;
// (0,1) is the screen-space x at which the split sits. Left of the split,
// the fragment ignores the PBR textures and falls back to the scalar
// ObjectData factors ("sentinel forced") so the viewer can compare plain
// shading vs full materials side by side. screenSize converts the
// fragment's framebuffer position into a [0,1] uv.
// All-scalar layout to match the C++ write byte-for-byte. A vec2 here would
// force 8-byte alignment on screenSize and desync from the tightly-packed
// C++ struct (abSplitX@0, screenW@4, screenH@8, pad@12).
struct FrameState {
    abSplitX: f32,
    screenW:  f32,
    screenH:  f32,
    _pad:     f32,
}
@group(2) @binding(6) var<uniform> frameState: FrameState;

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
    @location(6) texCoord:  vec2<f32>,
    @location(7) tangent:   vec3<f32>,  // 0 vector signals "no tangent supplied" → skip TBN
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
    out.texCoord  = input.texCoord;
    // Pass tangent unnormalized so the zero-vector sentinel (no tangent
    // supplied by the source asset, e.g. the procedural cube) survives the
    // rasterizer interpolation. The fragment checks length() before using.
    out.tangent   = normalMat * input.tangent;
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
    let Nvert = normalize(input.normal);

    // Step 9: A/B material toggle. Left of the split, force the "no texture"
    // path — scalar ObjectData factors only, vertex normal, no emissive —
    // so the viewer can compare plain shading against full PBR on the same
    // asset. abSplitX = 0 disables the split (full PBR everywhere).
    let abActive       = frameState.abSplitX > 0.0;
    let uvScreenX      = input.position.x / frameState.screenW;
    let onBaselineSide = abActive && uvScreenX < frameState.abSplitX;

    // ---- Full-PBR sampling (right of split, or split disabled) ----------

    // Step 6a: baseColor × factor. Buildings get a 1×1 white dummy × grey
    // factor = grey; the helmet's showcase bind group supplies the real
    // glTF baseColor with a white factor so the texture passes through.
    let baseColorSample = textureSample(baseColorTex, materialSampler, input.texCoord);
    let albedoTextured  = baseColorSample.rgb * input.albedo;

    // Step 6b: tangent-space normal mapping. Path A — per-vertex tangents
    // present: build a Gram-Schmidt-orthogonalized TBN and rotate the
    // sampled tangent-space normal into world space. Path B — no tangent
    // (procedural cube): the default normal decodes to (0,0,1) so the
    // vertex normal is already correct.
    let nMapTangentSpace = textureSample(normalTex, materialSampler, input.texCoord).rgb
                            * 2.0 - vec3<f32>(1.0);
    var Ntextured: vec3<f32>;
    if (length(input.tangent) > 0.01) {
        let T      = normalize(input.tangent);
        let Tortho = normalize(T - Nvert * dot(Nvert, T));
        let B      = cross(Nvert, Tortho);
        let TBN    = mat3x3<f32>(Tortho, B, Nvert);
        Ntextured  = normalize(TBN * nMapTangentSpace);
    } else {
        Ntextured = Nvert;
    }

    // Step 6c: MR (G=roughness, B=metallic) + occlusion (R). Dummies are
    // identity so the factor passes through when no texture is bound.
    let mrSample        = textureSample(mrTex, materialSampler, input.texCoord);
    let roughnessTextured = input.roughness * mrSample.g;
    let metallicTextured  = input.metallic  * mrSample.b;
    let aoTextured        = input.ao * textureSample(aoTex, materialSampler, input.texCoord).r;

    // Step 6d: emissive (sRGB slot → linear sample). Dummy is black.
    let emissiveTextured = textureSample(emissiveTex, materialSampler, input.texCoord).rgb;

    // ---- Select between textured and sentinel-forced (baseline) ---------
    // Baseline uses the raw ObjectData factors: no texture multiply, vertex
    // normal, no self-emission. This is exactly what the engine produced
    // before step 6, so the A/B divider shows the material work directly.
    let N         = select(Ntextured,          Nvert,          onBaselineSide);
    let albedo    = select(albedoTextured,     input.albedo,   onBaselineSide);
    let roughness = select(roughnessTextured,  input.roughness, onBaselineSide);
    let metallic  = select(metallicTextured,   input.metallic, onBaselineSide);
    let ao        = select(aoTextured,         input.ao,       onBaselineSide);
    let emissive  = select(emissiveTextured,   vec3<f32>(0.0), onBaselineSide);

    var out: GBufferOutput;
    out.gBuffer0 = vec4<f32>(N,        roughness);
    out.gBuffer1 = vec4<f32>(albedo,   metallic);
    out.gBuffer2 = vec4<f32>(ao, emissive);  // .r=ao, .gba=emissive RGB
    return out;
}
