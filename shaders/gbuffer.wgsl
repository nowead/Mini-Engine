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

    // Step 6a: baseColor sampling. Buildings get a 1×1 white dummy ×
    // grey factor = grey; the helmet rebinds its glTF baseColor for the
    // showcase draw and the factor stays white, so the texture flows
    // through unmodified. Sampler returns linear color (the layout uses
    // an sRGB format for the baseColor slot).
    let baseColorSample = textureSample(baseColorTex, materialSampler, input.texCoord);
    let albedoLinear    = baseColorSample.rgb * input.albedo;

    // Step 6b: tangent-space normal mapping. The default normal texture is
    // a 1×1 flat (0.5, 0.5, 1.0) so its decoded tangent-space normal is
    // (0, 0, 1); for an asset with a real glTF normal map the bind group
    // is rebound for the showcase draw.
    //
    // Path A — the source asset supplied per-vertex tangents (length(T) > 0):
    //   construct a TBN from the interpolated T and N and rotate the
    //   tangent-space sample into world space. Bitangent sign (glTF
    //   tangent.w) is not yet stored on the Vertex; this works on every
    //   DamagedHelmet-style asset without mirrored UVs and is the next
    //   thing to wire if we hit an asset that needs it.
    //
    // Path B — no tangent stream (e.g. the procedural cube). The default
    //   normal texture decodes to (0,0,1), so the correct world-space
    //   normal is exactly the vertex normal; skip TBN entirely.
    let nMapTangentSpace = textureSample(normalTex, materialSampler, input.texCoord).rgb
                            * 2.0 - vec3<f32>(1.0);
    var N: vec3<f32>;
    if (length(input.tangent) > 0.01) {
        let T = normalize(input.tangent);
        // Re-orthogonalize T against N (Gram-Schmidt) — rasterizer
        // interpolation can drift the two slightly out of orthogonality.
        let Tortho = normalize(T - Nvert * dot(Nvert, T));
        let B      = cross(Nvert, Tortho);
        let TBN    = mat3x3<f32>(Tortho, B, Nvert);
        N = normalize(TBN * nMapTangentSpace);
    } else {
        N = Nvert;
    }

    // Step 6c: metallic-roughness + occlusion sampling. glTF MR convention:
    //   G = roughness, B = metallic, R unused (sometimes packed AO for ORM
    //   assets, but the standard spec reads occlusion from a separate
    //   texture so we honor that here). Dummy MR is (*,1,1,*) so the
    //   factor passes through unchanged when no asset texture is bound.
    let mrSample  = textureSample(mrTex, materialSampler, input.texCoord);
    let roughness = input.roughness * mrSample.g;
    let metallic  = input.metallic  * mrSample.b;

    // Occlusion (R channel of the glTF occlusionTexture). Dummy AO is
    // (1,*,*,*) so factor passes through.
    let aoSample = textureSample(aoTex, materialSampler, input.texCoord).r;
    let ao       = input.ao * aoSample;

    // Step 6d: emissive sampling. glTF emissiveTexture is sRGB; the bind
    // layout uses RGBA8UnormSrgb for the emissive slot so textureSample
    // already returns linear color. Dummy is black so non-emissive assets
    // contribute nothing. We pack into gBuffer2's free gba channels (the
    // deferred lighting reader currently only consumes .r for AO), which
    // avoids needing a 4th render target. LDR-only because gBuffer2 is
    // RGBA8Unorm — HDR-bright emissive would need a real RGBA16Float
    // attachment as a follow-up.
    let emissive = textureSample(emissiveTex, materialSampler, input.texCoord).rgb;

    var out: GBufferOutput;
    out.gBuffer0 = vec4<f32>(N,            roughness);
    out.gBuffer1 = vec4<f32>(albedoLinear, metallic);
    out.gBuffer2 = vec4<f32>(ao, emissive);  // .r=ao, .gba=emissive RGB
    return out;
}
