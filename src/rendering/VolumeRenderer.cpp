#include "VolumeRenderer.hpp"
#include "src/utils/Logger.hpp"
#include "src/utils/FileUtils.hpp"

#include <glm/gtc/packing.hpp>   // packHalf1x16 (R8 -> R16Float upload conversion)

#include <cmath>
#include <cstring>
#include <limits>

namespace rendering {

VolumeRenderer::VolumeRenderer(rhi::RHIDevice* device, rhi::RHIQueue* graphicsQueue)
    : m_device(device), m_graphicsQueue(graphicsQueue) {}

// ---------------------------------------------------------------------------
// Procedural density field
// ---------------------------------------------------------------------------

namespace {
// Cheap integer hash -> [0,1), used for value noise.
float hash3(int x, int y, int z) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u
               + static_cast<uint32_t>(y) * 668265263u
               + static_cast<uint32_t>(z) * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h) / 4294967295.0f;
}

float smoothstep01(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

// Trilinearly-interpolated value noise at a grid frequency.
float valueNoise(float x, float y, float z) {
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const int iz = static_cast<int>(std::floor(z));
    const float fx = smoothstep01(x - ix);
    const float fy = smoothstep01(y - iy);
    const float fz = smoothstep01(z - iz);

    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    const float c000 = hash3(ix,   iy,   iz);
    const float c100 = hash3(ix+1, iy,   iz);
    const float c010 = hash3(ix,   iy+1, iz);
    const float c110 = hash3(ix+1, iy+1, iz);
    const float c001 = hash3(ix,   iy,   iz+1);
    const float c101 = hash3(ix+1, iy,   iz+1);
    const float c011 = hash3(ix,   iy+1, iz+1);
    const float c111 = hash3(ix+1, iy+1, iz+1);
    return lerp(lerp(lerp(c000, c100, fx), lerp(c010, c110, fx), fy),
                lerp(lerp(c001, c101, fx), lerp(c011, c111, fx), fy), fz);
}
} // namespace

void VolumeRenderer::generateProceduralVolume(std::vector<uint8_t>& out, uint32_t resolution) const {
    out.resize(static_cast<size_t>(resolution) * resolution * resolution);

    const float inv = 1.0f / static_cast<float>(resolution - 1);
    const float sphereRadius = 0.40f;

    for (uint32_t z = 0; z < resolution; ++z) {
        for (uint32_t y = 0; y < resolution; ++y) {
            for (uint32_t x = 0; x < resolution; ++x) {
                // Normalized [-0.5, 0.5] coordinates centered in the cube.
                const float nx = x * inv - 0.5f;
                const float ny = y * inv - 0.5f;
                const float nz = z * inv - 0.5f;
                const float dist = std::sqrt(nx*nx + ny*ny + nz*nz);

                // Soft sphere falloff: 1 at center -> 0 at sphereRadius.
                float shell = 1.0f - smoothstep01(dist / sphereRadius);

                // 3 octaves of value noise for a cloud-like surface.
                const float fx = x * inv, fy = y * inv, fz = z * inv;
                float n = 0.5f  * valueNoise(fx * 4.0f,  fy * 4.0f,  fz * 4.0f)
                        + 0.35f * valueNoise(fx * 8.0f,  fy * 8.0f,  fz * 8.0f)
                        + 0.15f * valueNoise(fx * 16.0f, fy * 16.0f, fz * 16.0f);

                float density = shell * (0.35f + 0.65f * n);
                density = density < 0.0f ? 0.0f : (density > 1.0f ? 1.0f : density);

                out[(static_cast<size_t>(z) * resolution + y) * resolution + x] =
                    static_cast<uint8_t>(density * 255.0f + 0.5f);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Upload
// ---------------------------------------------------------------------------

bool VolumeRenderer::uploadVolume(const std::vector<uint8_t>& density,
                                  uint32_t w, uint32_t h, uint32_t d) {
    if (density.size() < static_cast<size_t>(w) * h * d) {
        LOG_ERROR("VolumeRenderer") << "density buffer too small for " << w << "x" << h << "x" << d;
        return false;
    }
    // R8 [0,255] source -> normalized [0,1] half-floats (procedural / synthetic path).
    std::vector<uint16_t> halfData(static_cast<size_t>(w) * h * d);
    for (size_t i = 0; i < halfData.size(); ++i)
        halfData[i] = glm::packHalf1x16(density[i] / 255.0f);
    return uploadHalf(halfData, w, h, d);
}

// Core upload: pre-packed R16Float half data -> 3D texture + view. R16Float is the
// dual-backend choice -- core and filterable on both Vulkan and WebGPU (16-bit
// unorm is NOT WebGPU core). This is the format foundation for real 16-bit CT/MRI.
bool VolumeRenderer::uploadHalf(const std::vector<uint16_t>& halfData,
                                uint32_t w, uint32_t h, uint32_t d) {
    m_volW = w; m_volH = h; m_volD = d;
    const auto* halfBytes = reinterpret_cast<const uint8_t*>(halfData.data());

    // WebGPU requires bytesPerRow to be a 256-byte multiple, so pad each row (and
    // the staging buffer) on that backend; Vulkan packs tightly. (See CLAUDE.md:
    // copyBufferToTexture bytesPerRow semantics.)
    const uint32_t tightBytesPerRow = w * 2;      // width * 2 bytes (R16Float)
#ifdef __EMSCRIPTEN__
    const uint32_t paddedBytesPerRow = (tightBytesPerRow + 255u) & ~255u;
#else
    const uint32_t paddedBytesPerRow = tightBytesPerRow;
#endif
    const uint64_t stagingSize =
        static_cast<uint64_t>(paddedBytesPerRow) * h * d;

    rhi::BufferDesc stagingDesc{};
    stagingDesc.size  = stagingSize;
    stagingDesc.usage = rhi::BufferUsage::CopySrc | rhi::BufferUsage::MapWrite;
    auto staging = m_device->createBuffer(stagingDesc);
    if (!staging) { LOG_ERROR("VolumeRenderer") << "staging buffer create failed"; return false; }
    {
        uint8_t* mapped = static_cast<uint8_t*>(staging->map());
#ifdef __EMSCRIPTEN__
        // Per-row copy honoring the padded stride across every depth slice.
        for (uint32_t z = 0; z < d; ++z) {
            for (uint32_t y = 0; y < h; ++y) {
                const uint64_t row = static_cast<uint64_t>(z) * h + y;
                std::memcpy(mapped + row * paddedBytesPerRow,
                            halfBytes + row * tightBytesPerRow,
                            tightBytesPerRow);
            }
        }
#else
        std::memcpy(mapped, halfBytes, stagingSize);
#endif
        staging->unmap();
    }

    rhi::TextureDesc texDesc{};
    texDesc.size          = rhi::Extent3D{w, h, d};
    texDesc.dimension     = rhi::TextureDimension::Texture3D;
    texDesc.format        = rhi::TextureFormat::R16Float;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount   = 1;
    texDesc.usage         = rhi::TextureUsage::CopyDst | rhi::TextureUsage::Sampled;
    texDesc.label         = "VolumeDensity3D";
    m_volumeTexture = m_device->createTexture(texDesc);
    if (!m_volumeTexture) { LOG_ERROR("VolumeRenderer") << "3D texture create failed"; return false; }

    auto encoder = m_device->createCommandEncoder();
    encoder->transitionTextureLayout(m_volumeTexture.get(),
                                     rhi::TextureLayout::Undefined,
                                     rhi::TextureLayout::TransferDst);

    rhi::BufferTextureCopyInfo bufferCopy{};
    bufferCopy.buffer       = staging.get();
    bufferCopy.offset       = 0;
#ifdef __EMSCRIPTEN__
    bufferCopy.bytesPerRow  = paddedBytesPerRow;  // bytes, 256-aligned (WebGPU)
    bufferCopy.rowsPerImage = h;                   // rows per depth slice
#else
    bufferCopy.bytesPerRow  = 0;   // tightly packed (Vulkan reads this as texels; 0 = packed)
    bufferCopy.rowsPerImage = 0;   // tightly packed slices
#endif

    rhi::TextureCopyInfo texCopy{};
    texCopy.texture  = m_volumeTexture.get();
    texCopy.mipLevel = 0;
    texCopy.origin   = {0, 0, 0};
    texCopy.aspect   = 0;

    encoder->copyBufferToTexture(bufferCopy, texCopy, rhi::Extent3D{w, h, d});

    encoder->transitionTextureLayout(m_volumeTexture.get(),
                                     rhi::TextureLayout::TransferDst,
                                     rhi::TextureLayout::ShaderReadOnly);

    auto cmd = encoder->finish();
    m_graphicsQueue->submit(cmd.get());
    m_graphicsQueue->waitIdle();  // one-shot upload; safe to stall here

    // 3D view.
    rhi::TextureViewDesc viewDesc{};
    viewDesc.format          = rhi::TextureFormat::R16Float;
    viewDesc.dimension       = rhi::TextureViewDimension::View3D;
    viewDesc.baseMipLevel    = 0;
    viewDesc.mipLevelCount   = 1;
    viewDesc.baseArrayLayer  = 0;
    viewDesc.arrayLayerCount = 1;
    m_volumeView = m_volumeTexture->createView(viewDesc);
    if (!m_volumeView) { LOG_ERROR("VolumeRenderer") << "3D view create failed"; return false; }

    return true;
}

bool VolumeRenderer::initialize(uint32_t resolution) {
    if (!m_device || !m_graphicsQueue) return false;
    m_resolution = resolution;

    std::vector<uint8_t> density;
    generateProceduralVolume(density, resolution);

    if (!uploadVolume(density, resolution, resolution, resolution)) return false;

    // Linear, clamp-to-edge sampler for the volume.
    rhi::SamplerDesc samplerDesc{};
    samplerDesc.magFilter    = rhi::FilterMode::Linear;
    samplerDesc.minFilter    = rhi::FilterMode::Linear;
    samplerDesc.addressModeU = rhi::AddressMode::ClampToEdge;
    samplerDesc.addressModeV = rhi::AddressMode::ClampToEdge;
    samplerDesc.addressModeW = rhi::AddressMode::ClampToEdge;
    m_sampler = m_device->createSampler(samplerDesc);
    if (!m_sampler) { LOG_ERROR("VolumeRenderer") << "sampler create failed"; return false; }

    // Nearest, clamp sampler for the depth buffer (point sampled for occlusion).
    rhi::SamplerDesc depthSamplerDesc{};
    depthSamplerDesc.magFilter    = rhi::FilterMode::Nearest;
    depthSamplerDesc.minFilter    = rhi::FilterMode::Nearest;
    depthSamplerDesc.addressModeU = rhi::AddressMode::ClampToEdge;
    depthSamplerDesc.addressModeV = rhi::AddressMode::ClampToEdge;
    depthSamplerDesc.addressModeW = rhi::AddressMode::ClampToEdge;
    m_depthSampler = m_device->createSampler(depthSamplerDesc);
    if (!m_depthSampler) { LOG_ERROR("VolumeRenderer") << "depth sampler create failed"; return false; }

    // Transfer-function LUT (256x1 RGBA8). Always bound; sampled only when useLUT=1
    // (non-Custom preset). Default contents are uninitialized -- applyPendingTFUpdate
    // refills on the first preset switch. Custom preset bypasses it.
    {
        rhi::TextureDesc lutDesc{};
        lutDesc.size          = rhi::Extent3D{256, 1, 1};
        lutDesc.dimension     = rhi::TextureDimension::Texture2D;
        lutDesc.format        = rhi::TextureFormat::RGBA8Unorm;
        lutDesc.mipLevelCount = 1;
        lutDesc.sampleCount   = 1;
        lutDesc.usage         = rhi::TextureUsage::CopyDst | rhi::TextureUsage::Sampled;
        lutDesc.label         = "VolumeTFLUT";
        m_lutTexture = m_device->createTexture(lutDesc);
        if (!m_lutTexture) { LOG_ERROR("VolumeRenderer") << "LUT texture create failed"; return false; }

        rhi::TextureViewDesc lvd{};
        lvd.format          = rhi::TextureFormat::RGBA8Unorm;
        lvd.dimension       = rhi::TextureViewDimension::View2D;
        lvd.baseMipLevel    = 0; lvd.mipLevelCount   = 1;
        lvd.baseArrayLayer  = 0; lvd.arrayLayerCount = 1;
        m_lutView = m_lutTexture->createView(lvd);
        if (!m_lutView) { LOG_ERROR("VolumeRenderer") << "LUT view create failed"; return false; }

        // Initial layout transition so the shader can sample it before any preset
        // upload runs (Custom preset is the default and never triggers an upload).
        auto enc = m_device->createCommandEncoder();
        enc->transitionTextureLayout(m_lutTexture.get(),
                                     rhi::TextureLayout::Undefined,
                                     rhi::TextureLayout::ShaderReadOnly);
        auto cmd = enc->finish();
        m_graphicsQueue->submit(cmd.get());
        m_graphicsQueue->waitIdle();
    }

    // Per-frame uniform buffers.
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        rhi::BufferDesc uboDesc{};
        uboDesc.size  = sizeof(VolumeUBO);
        uboDesc.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::MapWrite;
        uboDesc.label = "VolumeUBO";
        m_uniformBuffers[i] = m_device->createBuffer(uboDesc);
        if (!m_uniformBuffers[i]) { LOG_ERROR("VolumeRenderer") << "UBO create failed"; return false; }
    }

    m_initialized = true;
    LOG_INFO("VolumeRenderer") << "initialized: " << resolution << "^3 R16Float density volume";
    return true;
}

// Generic data source: replace the volume texture with an external R8 density
// buffer of arbitrary dimensions. This is the seam between the engine (which only
// knows "a 3D density texture") and any data source -- procedural, a raw file
// loader, a future DICOM/NIfTI importer, etc. Safe to call at runtime: the texture
// + view are recreated and the bind groups rebuilt against the stored depth view.
bool VolumeRenderer::loadFromData(const std::vector<uint8_t>& density,
                                  uint32_t w, uint32_t h, uint32_t d) {
    if (!m_device || !m_graphicsQueue || w == 0 || h == 0 || d == 0) return false;
    if (!uploadVolume(density, w, h, d)) return false;   // recreates m_volumeTexture + m_volumeView
    if (m_bindGroupLayout && m_depthView) {
        if (!createBindGroups(m_depthView)) return false; // rebind the new view
    }
    LOG_INFO("VolumeRenderer") << "loaded volume data " << w << "x" << h << "x" << d;
    return true;
}

// Float-intensity data source (e.g. NIfTI/DICOM in Hounsfield Units). Stores the
// raw intensity directly in R16Float so window/level operates in the data's own
// units; tracks the data range and defaults the window to span it (no clipping).
bool VolumeRenderer::loadFromFloatData(const std::vector<float>& intensity,
                                       uint32_t w, uint32_t h, uint32_t d) {
    if (!m_device || !m_graphicsQueue || w == 0 || h == 0 || d == 0) return false;
    if (intensity.size() < static_cast<size_t>(w) * h * d) {
        LOG_ERROR("VolumeRenderer") << "intensity buffer too small for " << w << "x" << h << "x" << d;
        return false;
    }
    std::vector<uint16_t> halfData(static_cast<size_t>(w) * h * d);
    float mn = std::numeric_limits<float>::max();
    float mx = std::numeric_limits<float>::lowest();
    for (size_t i = 0; i < halfData.size(); ++i) {
        const float v = intensity[i];
        halfData[i] = glm::packHalf1x16(v);
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    m_dataMin = mn;
    m_dataMax = mx;
    // Default window spans the full data range (shows everything) until narrowed.
    m_windowCenter = 0.5f * (mn + mx);
    m_windowWidth  = (mx > mn) ? (mx - mn) : 1.0f;

    if (!uploadHalf(halfData, w, h, d)) return false;
    if (m_bindGroupLayout && m_depthView) {
        if (!createBindGroups(m_depthView)) return false;  // rebind the new view
    }
    LOG_INFO("VolumeRenderer") << "loaded float volume " << w << "x" << h << "x" << d
                               << " range [" << mn << "," << mx << "]";
    return true;
}

// ---------------------------------------------------------------------------
// Phase 7-3: ray-march pipeline + bind groups + UBO update
// ---------------------------------------------------------------------------

bool VolumeRenderer::createPipeline(rhi::RHITextureView* depthView, void* nativeRenderPass,
                                    rhi::TextureFormat colorFormat) {
#ifdef __EMSCRIPTEN__
    {
        auto raw = FileUtils::readFile("shaders/volume_march.wgsl");
        if (raw.empty()) { LOG_ERROR("VolumeRenderer") << "missing volume_march.wgsl"; return false; }
        std::vector<uint8_t> code(raw.begin(), raw.end());
        rhi::ShaderSource vs(rhi::ShaderLanguage::WGSL, code, rhi::ShaderStage::Vertex,   "vs_main");
        rhi::ShaderSource fs(rhi::ShaderLanguage::WGSL, code, rhi::ShaderStage::Fragment, "fs_main");
        m_vertexShader   = m_device->createShader(rhi::ShaderDesc(vs, "VolumeMarchVS"));
        m_fragmentShader = m_device->createShader(rhi::ShaderDesc(fs, "VolumeMarchFS"));
    }
#else
    auto loadSpv = [&](const char* path, rhi::ShaderStage stage, const char* label) {
        auto raw = FileUtils::readFile(path);
        if (raw.empty()) { LOG_ERROR("VolumeRenderer") << "missing " << path; return std::unique_ptr<rhi::RHIShader>{}; }
        std::vector<uint8_t> code(raw.begin(), raw.end());
        rhi::ShaderSource src(rhi::ShaderLanguage::SPIRV, code, stage, "main");
        return m_device->createShader(rhi::ShaderDesc(src, label));
    };
    m_vertexShader   = loadSpv("shaders/volume_march.vert.spv", rhi::ShaderStage::Vertex,   "VolumeMarchVS");
    m_fragmentShader = loadSpv("shaders/volume_march.frag.spv", rhi::ShaderStage::Fragment, "VolumeMarchFS");
#endif
    if (!m_vertexShader || !m_fragmentShader) return false;

    using S = rhi::ShaderStage;
    using T = rhi::BindingType;
    using D = rhi::TextureViewDimension;
    auto entry = [](uint32_t b, S s, T t, D dim = D::View2D) {
        rhi::BindGroupLayoutEntry e(b, s, t);
        e.textureViewDimension = dim;
        return e;
    };

    rhi::BindGroupLayoutDesc layoutDesc;
#ifdef __EMSCRIPTEN__
    // WebGPU: depth is texture_depth_2d read via textureLoad (no sampler).
    layoutDesc.entries = {
        entry(0, S::Fragment, T::UniformBuffer),
        entry(1, S::Fragment, T::DepthTexture,   D::View2D),  // scene depth (textureLoad)
        entry(2, S::Fragment, T::SampledTexture, D::View3D),  // volume density
        entry(3, S::Fragment, T::Sampler),                    // volume + LUT sampler (linear)
        entry(4, S::Fragment, T::SampledTexture, D::View2D),  // transfer-function LUT (256x1)
    };
#else
    // Vulkan: GLSL texelFetch(sampler2D(depth, sampler)) needs a depth sampler.
    layoutDesc.entries = {
        entry(0, S::Fragment, T::UniformBuffer),
        entry(1, S::Fragment, T::SampledTexture, D::View2D),  // scene depth
        entry(2, S::Fragment, T::Sampler),                    // depth sampler (nearest)
        entry(3, S::Fragment, T::SampledTexture, D::View3D),  // volume density
        entry(4, S::Fragment, T::Sampler),                    // volume + LUT sampler (linear)
        entry(5, S::Fragment, T::SampledTexture, D::View2D),  // transfer-function LUT (256x1)
    };
#endif
    layoutDesc.label = "VolumeBGLayout";
    m_bindGroupLayout = m_device->createBindGroupLayout(layoutDesc);
    if (!m_bindGroupLayout) return false;

    rhi::PipelineLayoutDesc plDesc;
    plDesc.bindGroupLayouts.push_back(m_bindGroupLayout.get());
    plDesc.label = "VolumePipelineLayout";
    m_pipelineLayout = m_device->createPipelineLayout(plDesc);
    if (!m_pipelineLayout) return false;

    rhi::RenderPipelineDesc pipelineDesc;
    pipelineDesc.label          = "VolumeMarchPipeline";
    pipelineDesc.layout         = m_pipelineLayout.get();
    pipelineDesc.vertexShader   = m_vertexShader.get();
    pipelineDesc.fragmentShader = m_fragmentShader.get();
    pipelineDesc.primitive.topology  = rhi::PrimitiveTopology::TriangleList;
    pipelineDesc.primitive.cullMode  = rhi::CullMode::None;
    pipelineDesc.primitive.frontFace = rhi::FrontFace::CounterClockwise;

    // Target with PREMULTIPLIED-alpha over blending (shader outputs premult rgb).
    // Format is caller-chosen: RGBA16Float when compositing into the main HDR
    // scene, or the swapchain format for a standalone viewer rendering directly.
    rhi::ColorTargetState ct;
    ct.format = colorFormat;
    ct.blend.blendEnabled   = true;
    ct.blend.srcColorFactor = rhi::BlendFactor::One;
    ct.blend.dstColorFactor = rhi::BlendFactor::OneMinusSrcAlpha;
    ct.blend.colorBlendOp   = rhi::BlendOp::Add;
    ct.blend.srcAlphaFactor = rhi::BlendFactor::One;
    ct.blend.dstAlphaFactor = rhi::BlendFactor::OneMinusSrcAlpha;
    ct.blend.alphaBlendOp   = rhi::BlendOp::Add;
    pipelineDesc.colorTargets = { ct };

    pipelineDesc.depthStencil    = nullptr;          // reads depth as a sampled texture
    pipelineDesc.nativeRenderPass = nativeRenderPass;

    m_pipeline = m_device->createRenderPipeline(pipelineDesc);
    if (!m_pipeline) { LOG_ERROR("VolumeRenderer") << "pipeline create failed"; return false; }

    return createBindGroups(depthView);
}

bool VolumeRenderer::createBindGroups(rhi::RHITextureView* depthView) {
    if (!m_bindGroupLayout || !depthView || !m_volumeView) return false;
    m_depthView = depthView;   // remembered so loadFromData() can rebind after a reload
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        rhi::BindGroupDesc desc;
        desc.layout = m_bindGroupLayout.get();
#ifdef __EMSCRIPTEN__
        desc.entries = {
            rhi::BindGroupEntry::Buffer(0, m_uniformBuffers[i].get(), 0, sizeof(VolumeUBO)),
            rhi::BindGroupEntry::TextureView(1, depthView),
            rhi::BindGroupEntry::TextureView(2, m_volumeView.get()),
            rhi::BindGroupEntry::Sampler(3, m_sampler.get()),
            rhi::BindGroupEntry::TextureView(4, m_lutView.get()),
        };
#else
        desc.entries = {
            rhi::BindGroupEntry::Buffer(0, m_uniformBuffers[i].get(), 0, sizeof(VolumeUBO)),
            rhi::BindGroupEntry::TextureView(1, depthView),
            rhi::BindGroupEntry::Sampler(2, m_depthSampler.get()),
            rhi::BindGroupEntry::TextureView(3, m_volumeView.get()),
            rhi::BindGroupEntry::Sampler(4, m_sampler.get()),
            rhi::BindGroupEntry::TextureView(5, m_lutView.get()),
        };
#endif
        desc.label = "VolumeBindGroup";
        m_bindGroups[i] = m_device->createBindGroup(desc);
        if (!m_bindGroups[i]) { LOG_ERROR("VolumeRenderer") << "bind group create failed"; return false; }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Transfer-function LUT: presets, generation, upload
// ---------------------------------------------------------------------------

const char* VolumeRenderer::tfPresetName(int i) {
    switch (static_cast<TFPreset>(i)) {
        case TFPreset::Custom:       return "Custom";
        case TFPreset::Cloud:        return "Cloud";
        case TFPreset::Fire:         return "Fire";
        case TFPreset::CTBone:       return "CT - Bone";
        case TFPreset::CTSoftTissue: return "CT - Soft Tissue";
        default: return "?";
    }
}

namespace {
struct TFKey { float t, r, g, b, a; };

// Piecewise-linear fill of a 256-entry RGBA8 LUT from control points.
void fillFromKeys(uint8_t* out256, const TFKey* keys, int n) {
    auto toByte = [](float v) {
        int x = static_cast<int>(v * 255.0f + 0.5f);
        return static_cast<uint8_t>(x < 0 ? 0 : (x > 255 ? 255 : x));
    };
    for (int i = 0; i < 256; ++i) {
        const float t = i / 255.0f;
        int s = 0;
        while (s + 1 < n - 1 && keys[s + 1].t < t) ++s;
        const TFKey& a = keys[s];
        const TFKey& b = keys[s + 1 < n ? s + 1 : s];
        const float dt = b.t - a.t;
        float u = dt > 1e-6f ? (t - a.t) / dt : 0.0f;
        if (u < 0.0f) u = 0.0f; else if (u > 1.0f) u = 1.0f;
        out256[i*4 + 0] = toByte(a.r + (b.r - a.r) * u);
        out256[i*4 + 1] = toByte(a.g + (b.g - a.g) * u);
        out256[i*4 + 2] = toByte(a.b + (b.b - a.b) * u);
        out256[i*4 + 3] = toByte(a.a + (b.a - a.a) * u);
    }
}
} // namespace

void VolumeRenderer::applyPendingTFUpdate() {
    if (!m_tfDirty || !m_lutTexture) return;
    m_tfDirty = false;

    std::vector<uint8_t> lut(256 * 4, 0);
    switch (m_tfPreset) {
        case TFPreset::Custom: {
            // Shader uses uniform 2-color path (useLUT=0); LUT contents unused, but
            // fill with the gradient for coherence.
            const TFKey k[] = {
                { 0.0f, m_lowColor.r,  m_lowColor.g,  m_lowColor.b,  0.0f },
                { 1.0f, m_highColor.r, m_highColor.g, m_highColor.b, 1.0f },
            };
            fillFromKeys(lut.data(), k, 2);
            break;
        }
        case TFPreset::Cloud: {
            const TFKey k[] = {
                { 0.00f, 0.85f, 0.88f, 0.95f, 0.00f },
                { 0.30f, 0.92f, 0.94f, 0.98f, 0.15f },
                { 1.00f, 1.00f, 1.00f, 1.00f, 1.00f },
            };
            fillFromKeys(lut.data(), k, 3);
            break;
        }
        case TFPreset::Fire: {
            const TFKey k[] = {
                { 0.00f, 0.05f, 0.00f, 0.10f, 0.00f },
                { 0.25f, 0.80f, 0.10f, 0.00f, 0.25f },
                { 0.55f, 1.00f, 0.55f, 0.05f, 0.60f },
                { 0.85f, 1.00f, 0.95f, 0.55f, 0.90f },
                { 1.00f, 1.00f, 1.00f, 0.90f, 1.00f },
            };
            fillFromKeys(lut.data(), k, 5);
            break;
        }
        case TFPreset::CTBone: {
            const TFKey k[] = {
                { 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },
                { 0.55f, 0.40f, 0.30f, 0.20f, 0.05f },
                { 0.80f, 0.95f, 0.90f, 0.80f, 0.50f },
                { 1.00f, 1.00f, 1.00f, 1.00f, 1.00f },
            };
            fillFromKeys(lut.data(), k, 4);
            break;
        }
        case TFPreset::CTSoftTissue: {
            const TFKey k[] = {
                { 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },
                { 0.25f, 0.60f, 0.25f, 0.20f, 0.20f },
                { 0.55f, 0.90f, 0.45f, 0.30f, 0.55f },
                { 0.85f, 0.70f, 0.30f, 0.20f, 0.35f },
                { 1.00f, 0.40f, 0.15f, 0.10f, 0.10f },
            };
            fillFromKeys(lut.data(), k, 5);
            break;
        }
        default: break;
    }

    // Upload (256x1 RGBA8 = 1024 bytes/row, already 256-aligned so both backends
    // accept the same bytesPerRow on WebGPU; Vulkan uses tightly-packed sentinel).
    const uint64_t lutBytes = 256ull * 4ull;
    rhi::BufferDesc sd{}; sd.size = lutBytes;
    sd.usage = rhi::BufferUsage::CopySrc | rhi::BufferUsage::MapWrite;
    auto staging = m_device->createBuffer(sd);
    if (!staging) return;
    { void* p = staging->map(); std::memcpy(p, lut.data(), lutBytes); staging->unmap(); }

    auto enc = m_device->createCommandEncoder();
    enc->transitionTextureLayout(m_lutTexture.get(),
                                 rhi::TextureLayout::ShaderReadOnly,
                                 rhi::TextureLayout::TransferDst);
    rhi::BufferTextureCopyInfo bc{};
    bc.buffer = staging.get(); bc.offset = 0;
#ifdef __EMSCRIPTEN__
    bc.bytesPerRow  = 1024; bc.rowsPerImage = 1;
#else
    bc.bytesPerRow  = 0;    bc.rowsPerImage = 0;
#endif
    rhi::TextureCopyInfo tc{};
    tc.texture = m_lutTexture.get(); tc.mipLevel = 0; tc.origin = {0, 0, 0}; tc.aspect = 0;
    enc->copyBufferToTexture(bc, tc, rhi::Extent3D{256, 1, 1});
    enc->transitionTextureLayout(m_lutTexture.get(),
                                 rhi::TextureLayout::TransferDst,
                                 rhi::TextureLayout::ShaderReadOnly);
    auto cmd = enc->finish();
    m_graphicsQueue->submit(cmd.get());
    m_graphicsQueue->waitIdle();  // one-shot LUT update; only on preset switch
}

void VolumeRenderer::updateUBO(uint32_t frameIndex, const glm::mat4& invView,
                               const glm::mat4& invProj, const glm::vec3& cameraPos) {
    const uint32_t fi = frameIndex % kFramesInFlight;
    if (!m_uniformBuffers[fi]) return;

    VolumeUBO ubo{};
    ubo.invView   = invView;
    ubo.invProj   = invProj;
    ubo.cameraPos = glm::vec4(cameraPos, 1.0f);
    ubo.aabbMin   = glm::vec4(m_aabbMin, 0.0f);
    ubo.aabbMax   = glm::vec4(m_aabbMax, 0.0f);
    ubo.params    = glm::vec4(m_stepSize, m_extinction, m_densityScale, m_maxSteps);
    const float useLUT   = (m_tfPreset == TFPreset::Custom) ? 0.0f : 1.0f;
    const float useDepth = m_useDepthOcclusion ? 1.0f : 0.0f;
    ubo.tf        = glm::vec4(m_tfThreshold, m_tfColorMix, useLUT, useDepth);
    ubo.lowColor  = glm::vec4(m_lowColor, 1.0f);
    ubo.highColor = glm::vec4(m_highColor, 1.0f);
    ubo.window    = glm::vec4(m_windowCenter, m_windowWidth, 0.0f, 0.0f);

    m_uniformBuffers[fi]->write(&ubo, sizeof(VolumeUBO));
}

} // namespace rendering
