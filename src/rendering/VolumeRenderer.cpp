#include "VolumeRenderer.hpp"
#include "src/utils/Logger.hpp"
#include "src/utils/FileUtils.hpp"

#include <glm/gtc/packing.hpp>   // packHalf1x16 (R8 -> R16Float upload conversion)

#include <algorithm>
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
    // Procedural density's "empty" is exact +0.0 (half-float 0x0000).
    return uploadHalf(halfData, w, h, d, /*emptyValueHalf=*/0x0000);
}

// Core upload: pre-packed R16Float half data -> bricked atlas + page table.
// Replaces the pre-M3-3 single dense 3D texture; every density read in the
// shaders now goes through a page-table lookup (see sampleVolume helper).
// emptyValueHalf is the half-float bit pattern that counts as "air" -- bricks
// whose 64^3 interior is uniformly that value never reach the atlas.
bool VolumeRenderer::uploadHalf(const std::vector<uint16_t>& halfData,
                                uint32_t w, uint32_t h, uint32_t d,
                                uint16_t emptyValueHalf,
                                glm::uvec3 atlasGridOverride) {
    m_volW = w; m_volH = h; m_volD = d;
    if (!m_brick.build(m_device, m_graphicsQueue, halfData, w, h, d,
                       emptyValueHalf, atlasGridOverride)) {
        LOG_ERROR("VolumeRenderer") << "brick atlas build failed";
        return false;
    }
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

    // Empty-space-skipping resources (compute pipeline once + first grid build).
    // Graceful: a missing shader leaves skipping off without breaking rendering.
    if (!createOccupancyResources())
        LOG_WARN("VolumeRenderer") << "occupancy compute unavailable -> empty-space skipping off";
    buildOccupancy();

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
    if (!uploadVolume(density, w, h, d)) return false;   // recreates brick atlas + page table
    buildOccupancy();                                    // rebuild skip grid for the new volume
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
                                       uint32_t w, uint32_t h, uint32_t d,
                                       glm::uvec3 atlasGridOverride) {
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

    // "Empty" for clinical CT/MR data is exactly the per-volume minimum (air at
    // -1000 HU for CT, background 0 for MR). Encode in half to match the atlas
    // storage so the per-brick all-equal test is bit-exact.
    const uint16_t emptyValueHalf = glm::packHalf1x16(mn);
    if (!uploadHalf(halfData, w, h, d, emptyValueHalf, atlasGridOverride)) return false;
    buildOccupancy();                                      // rebuild skip grid for the new volume
    if (m_bindGroupLayout && m_depthView) {
        if (!createBindGroups(m_depthView)) return false;  // rebind the new view
    }
    LOG_INFO("VolumeRenderer") << "loaded float volume " << w << "x" << h << "x" << d
                               << " range [" << mn << "," << mx << "]";
    return true;
}

// ---------------------------------------------------------------------------
// M3: empty-space skipping -- compute-built min/max occupancy grid
// ---------------------------------------------------------------------------

bool VolumeRenderer::createOccupancyResources() {
#ifdef __EMSCRIPTEN__
    auto raw = FileUtils::readFile("shaders/volume_occupancy.comp.wgsl");
    if (raw.empty()) { LOG_ERROR("VolumeRenderer") << "missing volume_occupancy.comp.wgsl"; return false; }
    std::vector<uint8_t> code(raw.begin(), raw.end());
    rhi::ShaderSource src(rhi::ShaderLanguage::WGSL, code, rhi::ShaderStage::Compute, "main");
#else
    auto raw = FileUtils::readFile("shaders/volume_occupancy.comp.spv");
    if (raw.empty()) { LOG_ERROR("VolumeRenderer") << "missing volume_occupancy.comp.spv"; return false; }
    std::vector<uint8_t> code(raw.begin(), raw.end());
    rhi::ShaderSource src(rhi::ShaderLanguage::SPIRV, code, rhi::ShaderStage::Compute, "main");
#endif
    m_occShader = m_device->createShader(rhi::ShaderDesc(src, "VolumeOccupancy"));
    if (!m_occShader) return false;

    using S = rhi::ShaderStage;
    using T = rhi::BindingType;
    using D = rhi::TextureViewDimension;
    rhi::BindGroupLayoutEntry volEntry(1, S::Compute, T::SampledTexture);
    volEntry.textureViewDimension = D::View3D;

    rhi::BindGroupLayoutDesc ld;
#ifdef __EMSCRIPTEN__
    ld.entries = {
        rhi::BindGroupLayoutEntry(0, S::Compute, T::UniformBuffer),
        volEntry,                                                       // brick atlas
        rhi::BindGroupLayoutEntry(2, S::Compute, T::StorageBuffer),     // occupancy out (read_write)
        rhi::BindGroupLayoutEntry(3, S::Compute, T::ReadOnlyStorageBuffer), // page table
    };
#else
    ld.entries = {
        rhi::BindGroupLayoutEntry(0, S::Compute, T::UniformBuffer),
        volEntry,                                                       // brick atlas
        rhi::BindGroupLayoutEntry(2, S::Compute, T::Sampler),           // texelFetch needs a combined sampler
        rhi::BindGroupLayoutEntry(3, S::Compute, T::StorageBuffer),     // occupancy out (read_write)
        rhi::BindGroupLayoutEntry(4, S::Compute, T::ReadOnlyStorageBuffer), // page table
    };
#endif
    ld.label = "VolumeOccLayout";
    m_occLayout = m_device->createBindGroupLayout(ld);
    if (!m_occLayout) return false;

    rhi::PipelineLayoutDesc pl;
    pl.bindGroupLayouts.push_back(m_occLayout.get());
    pl.label = "VolumeOccPipelineLayout";
    m_occPipelineLayout = m_device->createPipelineLayout(pl);
    if (!m_occPipelineLayout) return false;

    rhi::ComputePipelineDesc cp(m_occShader.get(), m_occPipelineLayout.get());
    cp.label = "VolumeOccPipeline";
    m_occPipeline = m_device->createComputePipeline(cp);
    return m_occPipeline != nullptr;
}

bool VolumeRenderer::buildOccupancy() {
    m_occReady = false;
    if (m_volW == 0) return true;

    m_gridW = (m_volW + kCellSize - 1) / kCellSize;
    m_gridH = (m_volH + kCellSize - 1) / kCellSize;
    m_gridD = (m_volD + kCellSize - 1) / kCellSize;
    const uint64_t cellCount = static_cast<uint64_t>(m_gridW) * m_gridH * m_gridD;
    const uint64_t bufBytes  = cellCount * 2 * sizeof(float);

    // (Re)create the grid buffer (always, so the march bind group is valid even if
    // the compute resources are missing -- in that case occ stays disabled).
    rhi::BufferDesc bd{};
    bd.size  = bufBytes;
    bd.usage = rhi::BufferUsage::Storage;
    bd.label = "VolumeOccGrid";
    m_occBuffer = m_device->createBuffer(bd);
    if (!m_occBuffer) { LOG_ERROR("VolumeRenderer") << "occupancy buffer create failed"; return false; }

    if (!m_occPipeline) return true;   // resources unavailable -> buffer exists, skipping off

    if (!m_occUBO) {
        rhi::BufferDesc ud{};
        ud.size  = sizeof(uint32_t) * 16;
        ud.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::MapWrite;
        ud.label = "VolumeOccUBO";
        m_occUBO = m_device->createBuffer(ud);
        if (!m_occUBO) return false;
    }
    // Layout matches the WGSL/GLSL volume_occupancy UBO declaration.
    const glm::uvec3 pg = m_brick.pageGrid();
    const glm::uvec3 ag = m_brick.atlasGrid();
    const uint32_t u[16] = {
        m_volW, m_volH, m_volD, kCellSize,
        m_gridW, m_gridH, m_gridD, 0,
        pg.x, pg.y, pg.z, 0,
        ag.x, ag.y, ag.z, 0,
    };
    m_occUBO->write(u, sizeof(u));

    rhi::BindGroupDesc bg;
    bg.layout = m_occLayout.get();
    const uint64_t pageBytes = m_brick.pageTableSize();
#ifdef __EMSCRIPTEN__
    bg.entries = {
        rhi::BindGroupEntry::Buffer(0, m_occUBO.get(), 0, sizeof(uint32_t) * 16),
        rhi::BindGroupEntry::TextureView(1, m_brick.atlasView()),
        rhi::BindGroupEntry::Buffer(2, m_occBuffer.get(), 0, bufBytes),
        rhi::BindGroupEntry::Buffer(3, m_brick.pageTable(), 0, pageBytes),
    };
#else
    bg.entries = {
        rhi::BindGroupEntry::Buffer(0, m_occUBO.get(), 0, sizeof(uint32_t) * 16),
        rhi::BindGroupEntry::TextureView(1, m_brick.atlasView()),
        rhi::BindGroupEntry::Sampler(2, m_sampler.get()),
        rhi::BindGroupEntry::Buffer(3, m_occBuffer.get(), 0, bufBytes),
        rhi::BindGroupEntry::Buffer(4, m_brick.pageTable(), 0, pageBytes),
    };
#endif
    bg.label = "VolumeOccBindGroup";
    m_occBindGroup = m_device->createBindGroup(bg);
    if (!m_occBindGroup) return false;

    auto enc = m_device->createCommandEncoder();
    auto pass = enc->beginComputePass("VolumeOccupancy");
    pass->setPipeline(m_occPipeline.get());
    pass->setBindGroup(0, m_occBindGroup.get());
    pass->dispatch((m_gridW + 3) / 4, (m_gridH + 3) / 4, (m_gridD + 3) / 4);
    pass->end();
    auto cmd = enc->finish();
    m_graphicsQueue->submit(cmd.get());
    m_graphicsQueue->waitIdle();   // one-shot build; safe to stall

    m_occReady = true;
    LOG_INFO("VolumeRenderer") << "occupancy grid " << m_gridW << "x" << m_gridH << "x" << m_gridD
                               << " (cell " << kCellSize << ")";
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
        entry(2, S::Fragment, T::SampledTexture, D::View3D),  // brick atlas
        entry(3, S::Fragment, T::Sampler),                    // atlas + LUT sampler (linear)
        entry(4, S::Fragment, T::SampledTexture, D::View2D),  // transfer-function LUT (256x1)
        rhi::BindGroupLayoutEntry(5, S::Fragment, T::ReadOnlyStorageBuffer),  // occupancy grid
        rhi::BindGroupLayoutEntry(6, S::Fragment, T::ReadOnlyStorageBuffer),  // brick page table
    };
#else
    // Vulkan: GLSL texelFetch(sampler2D(depth, sampler)) needs a depth sampler.
    layoutDesc.entries = {
        entry(0, S::Fragment, T::UniformBuffer),
        entry(1, S::Fragment, T::SampledTexture, D::View2D),  // scene depth
        entry(2, S::Fragment, T::Sampler),                    // depth sampler (nearest)
        entry(3, S::Fragment, T::SampledTexture, D::View3D),  // brick atlas
        entry(4, S::Fragment, T::Sampler),                    // atlas + LUT sampler (linear)
        entry(5, S::Fragment, T::SampledTexture, D::View2D),  // transfer-function LUT (256x1)
        rhi::BindGroupLayoutEntry(6, S::Fragment, T::ReadOnlyStorageBuffer),  // occupancy grid
        rhi::BindGroupLayoutEntry(7, S::Fragment, T::ReadOnlyStorageBuffer),  // brick page table
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

    // M4 v1: the path-trace + display pipelines live in createAccumulationResources
    // because they need the swapchain width/height (for the accumulation textures)
    // and a separate bind-group layout (no depth/occupancy, adds the history tex).
    return createBindGroups(depthView);
}

bool VolumeRenderer::createBindGroups(rhi::RHITextureView* depthView) {
    if (!m_bindGroupLayout || !depthView || !m_brick.atlasView() || !m_occBuffer || !m_brick.pageTable()) return false;
    m_depthView = depthView;   // remembered so loadFromData() can rebind after a reload
    const uint64_t pageBytes = m_brick.pageTableSize();
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        rhi::BindGroupDesc desc;
        desc.layout = m_bindGroupLayout.get();
        const uint64_t occBytes = static_cast<uint64_t>(m_gridW) * m_gridH * m_gridD * 2 * sizeof(float);
#ifdef __EMSCRIPTEN__
        desc.entries = {
            rhi::BindGroupEntry::Buffer(0, m_uniformBuffers[i].get(), 0, sizeof(VolumeUBO)),
            rhi::BindGroupEntry::TextureView(1, depthView),
            rhi::BindGroupEntry::TextureView(2, m_brick.atlasView()),
            rhi::BindGroupEntry::Sampler(3, m_sampler.get()),
            rhi::BindGroupEntry::TextureView(4, m_lutView.get()),
            rhi::BindGroupEntry::Buffer(5, m_occBuffer.get(), 0, occBytes),
            rhi::BindGroupEntry::Buffer(6, m_brick.pageTable(), 0, pageBytes),
        };
#else
        desc.entries = {
            rhi::BindGroupEntry::Buffer(0, m_uniformBuffers[i].get(), 0, sizeof(VolumeUBO)),
            rhi::BindGroupEntry::TextureView(1, depthView),
            rhi::BindGroupEntry::Sampler(2, m_depthSampler.get()),
            rhi::BindGroupEntry::TextureView(3, m_brick.atlasView()),
            rhi::BindGroupEntry::Sampler(4, m_sampler.get()),
            rhi::BindGroupEntry::TextureView(5, m_lutView.get()),
            rhi::BindGroupEntry::Buffer(6, m_occBuffer.get(), 0, occBytes),
            rhi::BindGroupEntry::Buffer(7, m_brick.pageTable(), 0, pageBytes),
        };
#endif
        desc.label = "VolumeBindGroup";
        m_bindGroups[i] = m_device->createBindGroup(desc);
        if (!m_bindGroups[i]) { LOG_ERROR("VolumeRenderer") << "bind group create failed"; return false; }
    }
    return true;
}

// ---------------------------------------------------------------------------
// M4 v1: path-trace + display pipelines, ping-pong accumulation textures.
// Called once after createPipeline, again on swapchain resize.
// ---------------------------------------------------------------------------
bool VolumeRenderer::createAccumulationResources(uint32_t width, uint32_t height,
                                                 rhi::TextureFormat swapchainFormat) {
    if (!m_device || !m_vertexShader || width == 0 || height == 0) return false;

    // 1. Two RGBA16Float accumulation textures (ping-pong: read prev, write current).
    for (uint32_t i = 0; i < 2; ++i) {
        rhi::TextureDesc td{};
        td.size          = rhi::Extent3D{width, height, 1};
        td.dimension     = rhi::TextureDimension::Texture2D;
        td.format        = rhi::TextureFormat::RGBA16Float;
        td.mipLevelCount = 1;
        td.sampleCount   = 1;
        td.usage         = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled;
        td.label         = "VolumePathAccum";
        m_accumTextures[i] = m_device->createTexture(td);
        if (!m_accumTextures[i]) { LOG_ERROR("VolumeRenderer") << "accum texture create failed"; return false; }

        rhi::TextureViewDesc vd{};
        vd.format          = rhi::TextureFormat::RGBA16Float;
        vd.dimension       = rhi::TextureViewDimension::View2D;
        vd.mipLevelCount   = 1;
        vd.arrayLayerCount = 1;
        m_accumViews[i] = m_accumTextures[i]->createView(vd);
        if (!m_accumViews[i]) return false;
    }
    if (!m_accumSampler) {
        rhi::SamplerDesc sd{};
        sd.magFilter    = rhi::FilterMode::Nearest;
        sd.minFilter    = rhi::FilterMode::Nearest;
        sd.addressModeU = rhi::AddressMode::ClampToEdge;
        sd.addressModeV = rhi::AddressMode::ClampToEdge;
        sd.addressModeW = rhi::AddressMode::ClampToEdge;
        m_accumSampler = m_device->createSampler(sd);
        if (!m_accumSampler) return false;
    }

    // 2. Load both path-trace shaders (lazy: once).
    if (!m_pathFragmentShader) {
#ifdef __EMSCRIPTEN__
        auto raw = FileUtils::readFile("shaders/volume_pathtrace.wgsl");
        if (raw.empty()) { LOG_ERROR("VolumeRenderer") << "missing volume_pathtrace.wgsl"; return false; }
        std::vector<uint8_t> code(raw.begin(), raw.end());
        rhi::ShaderSource fs(rhi::ShaderLanguage::WGSL, code, rhi::ShaderStage::Fragment, "fs_main");
        m_pathFragmentShader = m_device->createShader(rhi::ShaderDesc(fs, "VolumePathtraceFS"));
#else
        auto raw = FileUtils::readFile("shaders/volume_pathtrace.frag.spv");
        if (raw.empty()) { LOG_ERROR("VolumeRenderer") << "missing volume_pathtrace.frag.spv"; return false; }
        std::vector<uint8_t> code(raw.begin(), raw.end());
        rhi::ShaderSource fs(rhi::ShaderLanguage::SPIRV, code, rhi::ShaderStage::Fragment, "main");
        m_pathFragmentShader = m_device->createShader(rhi::ShaderDesc(fs, "VolumePathtraceFS"));
#endif
        if (!m_pathFragmentShader) return false;
    }
    if (!m_pathDisplayFragmentShader) {
#ifdef __EMSCRIPTEN__
        auto raw = FileUtils::readFile("shaders/volume_pathtrace_display.wgsl");
        if (raw.empty()) { LOG_ERROR("VolumeRenderer") << "missing volume_pathtrace_display.wgsl"; return false; }
        std::vector<uint8_t> code(raw.begin(), raw.end());
        rhi::ShaderSource fs(rhi::ShaderLanguage::WGSL, code, rhi::ShaderStage::Fragment, "fs_main");
        m_pathDisplayFragmentShader = m_device->createShader(rhi::ShaderDesc(fs, "VolumePathDisplayFS"));
#else
        auto raw = FileUtils::readFile("shaders/volume_pathtrace_display.frag.spv");
        if (raw.empty()) { LOG_ERROR("VolumeRenderer") << "missing volume_pathtrace_display.frag.spv"; return false; }
        std::vector<uint8_t> code(raw.begin(), raw.end());
        rhi::ShaderSource fs(rhi::ShaderLanguage::SPIRV, code, rhi::ShaderStage::Fragment, "main");
        m_pathDisplayFragmentShader = m_device->createShader(rhi::ShaderDesc(fs, "VolumePathDisplayFS"));
#endif
        if (!m_pathDisplayFragmentShader) return false;
    }

    // 3. Path-trace bind-group layout (lazy: once). Different from march -- no
    //    depth or occupancy bindings; adds the previous accumulation texture.
    if (!m_pathBindGroupLayout) {
        using S = rhi::ShaderStage;
        using T = rhi::BindingType;
        using D = rhi::TextureViewDimension;
        auto entry = [](uint32_t b, S s, T t, D dim = D::View2D) {
            rhi::BindGroupLayoutEntry e(b, s, t);
            e.textureViewDimension = dim;
            return e;
        };
        rhi::BindGroupLayoutDesc ld;
        ld.entries = {
            entry(0, S::Fragment, T::UniformBuffer),
            entry(1, S::Fragment, T::SampledTexture, D::View3D),  // brick atlas
            entry(2, S::Fragment, T::Sampler),                    // atlas + LUT sampler
            entry(3, S::Fragment, T::SampledTexture, D::View2D),  // TF LUT
            entry(4, S::Fragment, T::SampledTexture, D::View2D),  // history (prev accumulation)
            rhi::BindGroupLayoutEntry(5, S::Fragment, T::ReadOnlyStorageBuffer), // brick page table
        };
        ld.label = "VolumePathBGLayout";
        m_pathBindGroupLayout = m_device->createBindGroupLayout(ld);
        if (!m_pathBindGroupLayout) return false;
        rhi::PipelineLayoutDesc pl;
        pl.bindGroupLayouts.push_back(m_pathBindGroupLayout.get());
        pl.label = "VolumePathPipelineLayout";
        m_pathPipelineLayout = m_device->createPipelineLayout(pl);
        if (!m_pathPipelineLayout) return false;
    }

    // 4. Path-trace pipeline (targets RGBA16Float accumulation; no blend).
    {
        rhi::RenderPipelineDesc pd;
        pd.label          = "VolumePathPipeline";
        pd.layout         = m_pathPipelineLayout.get();
        pd.vertexShader   = m_vertexShader.get();
        pd.fragmentShader = m_pathFragmentShader.get();
        pd.primitive.topology  = rhi::PrimitiveTopology::TriangleList;
        pd.primitive.cullMode  = rhi::CullMode::None;
        pd.primitive.frontFace = rhi::FrontFace::CounterClockwise;
        rhi::ColorTargetState ct;
        ct.format             = rhi::TextureFormat::RGBA16Float;
        ct.blend.blendEnabled = false;
        pd.colorTargets = { ct };
        pd.depthStencil = nullptr;
        pd.nativeRenderPass = nullptr;   // dynamic rendering on Vulkan; n/a on WebGPU
        m_pathPipeline = m_device->createRenderPipeline(pd);
        if (!m_pathPipeline) { LOG_ERROR("VolumeRenderer") << "path-trace pipeline create failed"; return false; }
    }

    // 5. Display bind-group layout + pipeline (target = swapchain).
    if (!m_pathDisplayLayout) {
        using S = rhi::ShaderStage;
        using T = rhi::BindingType;
        rhi::BindGroupLayoutDesc ld;
#ifdef __EMSCRIPTEN__
        // WGSL textureLoad needs no sampler.
        ld.entries = {
            rhi::BindGroupLayoutEntry(0, S::Fragment, T::SampledTexture),
        };
#else
        // GLSL texelFetch(sampler2D(...)) needs a sampler.
        ld.entries = {
            rhi::BindGroupLayoutEntry(0, S::Fragment, T::SampledTexture),
            rhi::BindGroupLayoutEntry(1, S::Fragment, T::Sampler),
        };
#endif
        ld.label = "VolumePathDisplayBGLayout";
        m_pathDisplayLayout = m_device->createBindGroupLayout(ld);
        if (!m_pathDisplayLayout) return false;
        rhi::PipelineLayoutDesc pl;
        pl.bindGroupLayouts.push_back(m_pathDisplayLayout.get());
        pl.label = "VolumePathDisplayPipelineLayout";
        m_pathDisplayPipelineLayout = m_device->createPipelineLayout(pl);
        if (!m_pathDisplayPipelineLayout) return false;
    }
    {
        rhi::RenderPipelineDesc pd;
        pd.label          = "VolumePathDisplayPipeline";
        pd.layout         = m_pathDisplayPipelineLayout.get();
        pd.vertexShader   = m_vertexShader.get();
        pd.fragmentShader = m_pathDisplayFragmentShader.get();
        pd.primitive.topology  = rhi::PrimitiveTopology::TriangleList;
        pd.primitive.cullMode  = rhi::CullMode::None;
        pd.primitive.frontFace = rhi::FrontFace::CounterClockwise;
        rhi::ColorTargetState ct;
        ct.format             = swapchainFormat;
        ct.blend.blendEnabled = false;
        pd.colorTargets = { ct };
        pd.depthStencil = nullptr;
        pd.nativeRenderPass = nullptr;
        m_pathDisplayPipeline = m_device->createRenderPipeline(pd);
        if (!m_pathDisplayPipeline) { LOG_ERROR("VolumeRenderer") << "display pipeline create failed"; return false; }
    }

    // 6. Bind groups (rebuild every resize because they reference the textures).
    const uint64_t pathPageBytes = m_brick.pageTable() ? m_brick.pageTableSize() : 0;
    for (uint32_t fi = 0; fi < kFramesInFlight; ++fi) {
        for (uint32_t pp = 0; pp < 2; ++pp) {
            rhi::BindGroupDesc desc;
            desc.layout = m_pathBindGroupLayout.get();
            desc.entries = {
                rhi::BindGroupEntry::Buffer(0, m_uniformBuffers[fi].get(), 0, sizeof(VolumeUBO)),
                rhi::BindGroupEntry::TextureView(1, m_brick.atlasView()),
                rhi::BindGroupEntry::Sampler(2, m_sampler.get()),
                rhi::BindGroupEntry::TextureView(3, m_lutView.get()),
                rhi::BindGroupEntry::TextureView(4, m_accumViews[pp].get()),  // read from this ping-pong slot
                rhi::BindGroupEntry::Buffer(5, m_brick.pageTable(), 0, pathPageBytes),
            };
            desc.label = "VolumePathBindGroup";
            m_pathBindGroups[fi][pp] = m_device->createBindGroup(desc);
            if (!m_pathBindGroups[fi][pp]) { LOG_ERROR("VolumeRenderer") << "path bind group create failed"; return false; }
        }
    }
    for (uint32_t pp = 0; pp < 2; ++pp) {
        rhi::BindGroupDesc desc;
        desc.layout = m_pathDisplayLayout.get();
#ifdef __EMSCRIPTEN__
        desc.entries = {
            rhi::BindGroupEntry::TextureView(0, m_accumViews[pp].get()),
        };
#else
        desc.entries = {
            rhi::BindGroupEntry::TextureView(0, m_accumViews[pp].get()),
            rhi::BindGroupEntry::Sampler(1, m_accumSampler.get()),
        };
#endif
        desc.label = "VolumePathDisplayBindGroup";
        m_pathDisplayBindGroups[pp] = m_device->createBindGroup(desc);
        if (!m_pathDisplayBindGroups[pp]) { LOG_ERROR("VolumeRenderer") << "display bind group create failed"; return false; }
    }

    // Resize implies a fresh accumulation. Garbage in the two textures is fine
    // because the integrator gates on N==0 (prev * 0 = 0), but reset the counter.
    resetAccumulation();
    LOG_INFO("VolumeRenderer") << "path-trace accumulation ready (" << width << "x" << height << ")";
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
    // Gradient-shading params. gradEps = ~1 voxel in [0,1] texture space (use the
    // largest dim so the step never exceeds one voxel on any axis).
    const uint32_t maxDim = std::max(m_volW, std::max(m_volH, m_volD));
    const float gradEps = (maxDim > 0) ? (1.0f / static_cast<float>(maxDim)) : (1.0f / 128.0f);
    const glm::vec3 L = glm::length(m_lightDir) > 1e-6f ? glm::normalize(m_lightDir) : glm::vec3(0, 1, 0);
    ubo.light = glm::vec4(L, m_shadingEnabled ? 1.0f : 0.0f);
    ubo.shade = glm::vec4(m_ambient, m_diffuse, gradEps, 0.0f);
    ubo.shadow = glm::vec4(m_shadowEnabled ? 1.0f : 0.0f, m_shadowStep,
                           m_shadowMaxSteps, m_shadowStrength);
    const float occEnable = (m_occEnabled && m_occReady) ? 1.0f : 0.0f;
    ubo.occ = glm::vec4(static_cast<float>(m_gridW), static_cast<float>(m_gridH),
                        static_cast<float>(m_gridD), occEnable);

    // M4: pack the path-tracer params + bump the frame seed so the PRNG decorrelates
    // each frame (without an accumulation buffer this is what hides the noise).
    ++m_frameSeed;
    float seedAsFloat;
    std::memcpy(&seedAsFloat, &m_frameSeed, sizeof(float));
    ubo.pathtrace = glm::vec4(static_cast<float>(m_pathSpp), m_pathG,
                              seedAsFloat, static_cast<float>(m_pathBounces));
    ubo.accum     = glm::vec4(m_pathSampleCount, 0.0f, 0.0f, 0.0f);
    // M3-3: brick descriptor for the shader's sampleVolume() indirection.
    const glm::uvec3 vs = m_brick.volSize();
    const glm::uvec3 ag = m_brick.atlasGrid();
    ubo.volSize   = glm::vec4(static_cast<float>(vs.x), static_cast<float>(vs.y),
                              static_cast<float>(vs.z), 0.0f);
    ubo.atlasGrid = glm::vec4(static_cast<float>(ag.x), static_cast<float>(ag.y),
                              static_cast<float>(ag.z), 0.0f);

    m_uniformBuffers[fi]->write(&ubo, sizeof(VolumeUBO));
}

// ---------------------------------------------------------------------------
// v1-1 brick streaming -- frustum-vs-brick visibility test on the CPU
// ---------------------------------------------------------------------------
namespace {

// Pack a clip-space plane (a*x + b*y + c*z + d >= 0 = inside).
struct Plane { float a, b, c, d; };

// Extract 6 frustum planes from a combined view-projection matrix in clip
// space. WebGPU/Vulkan use NDC z = [0, 1], so the near-plane derivation is
// row(2) on its own (vs OpenGL's row(2)+row(3)).
inline std::array<Plane, 6> extractFrustumPlanes(const glm::mat4& vp) {
    // Row-major access: glm matrices are column-major in storage, vp[col][row].
    auto rowEq = [&](int row) -> glm::vec4 {
        return glm::vec4(vp[0][row], vp[1][row], vp[2][row], vp[3][row]);
    };
    const glm::vec4 r0 = rowEq(0);
    const glm::vec4 r1 = rowEq(1);
    const glm::vec4 r2 = rowEq(2);
    const glm::vec4 r3 = rowEq(3);
    std::array<Plane, 6> p;
    auto setPlane = [&](Plane& dst, const glm::vec4& v) {
        // Normalise so the AABB test below stays in linear units.
        const float n = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        const float inv = (n > 1e-6f) ? (1.0f / n) : 0.0f;
        dst = { v.x * inv, v.y * inv, v.z * inv, v.w * inv };
    };
    setPlane(p[0], r3 + r0);   // left
    setPlane(p[1], r3 - r0);   // right
    setPlane(p[2], r3 + r1);   // bottom
    setPlane(p[3], r3 - r1);   // top
    setPlane(p[4], r2);        // near (NDC z >= 0)
    setPlane(p[5], r3 - r2);   // far  (NDC z <= 1)
    return p;
}

// Conservative AABB-vs-frustum: returns true if any portion of the AABB
// can be inside the frustum. Uses the p-vertex (most positive along plane
// normal) trick -- if the p-vertex is below a plane, the whole AABB is
// outside that plane and we cull. Misses no visible boxes; may keep some
// just-outside ones (acceptable for streaming -- a one-frame extra brick
// resident costs negligible).
inline bool aabbIntersectsFrustum(const std::array<Plane, 6>& planes,
                                  const glm::vec3& mn, const glm::vec3& mx) {
    for (const Plane& p : planes) {
        const float px = (p.a >= 0.0f) ? mx.x : mn.x;
        const float py = (p.b >= 0.0f) ? mx.y : mn.y;
        const float pz = (p.c >= 0.0f) ? mx.z : mn.z;
        if (p.a * px + p.b * py + p.c * pz + p.d < 0.0f) return false;
    }
    return true;
}

} // namespace

BrickedVolume::StreamUpdateStats
VolumeRenderer::updateBrickStreaming(const glm::mat4& view,
                                      const glm::mat4& proj,
                                      uint64_t /*frameIdx*/) {
    BrickedVolume::StreamUpdateStats stats{};
    const glm::uvec3 pg = m_brick.pageGrid();
    if (pg.x == 0 || pg.y == 0 || pg.z == 0) return stats;

    const std::array<Plane, 6> planes = extractFrustumPlanes(proj * view);
    const glm::vec3 boxSize = m_aabbMax - m_aabbMin;
    const glm::vec3 brickSize(boxSize.x / static_cast<float>(pg.x),
                              boxSize.y / static_cast<float>(pg.y),
                              boxSize.z / static_cast<float>(pg.z));

    // Two queries per visible brick:
    //   pageHasData(idx): does the source brick contain non-air voxels?
    //   pageTableHost[idx] != kEmptySlot: is that brick currently in the atlas?
    // Static mode: those two agree -- non-empty bricks are always resident.
    // Streaming mode (v1-2): pageTableHost is all sentinel, so visibleMissing
    // equals visibleNonEmpty until v1-3 lands. visibleResident climbs as v1-3
    // pages bricks in.
    const auto& pageHost = m_brick.pageTableHost();
    const bool haveHost = !pageHost.empty();

    for (uint32_t bz = 0; bz < pg.z; ++bz) {
        for (uint32_t by = 0; by < pg.y; ++by) {
            for (uint32_t bx = 0; bx < pg.x; ++bx) {
                const glm::vec3 mn = m_aabbMin + glm::vec3(bx, by, bz) * brickSize;
                const glm::vec3 mx = mn + brickSize;
                if (!aabbIntersectsFrustum(planes, mn, mx)) continue;
                ++stats.visibleBricks;
                const uint32_t idx = (bz * pg.y + by) * pg.x + bx;
                if (m_brick.pageHasData(idx)) {
                    ++stats.visibleNonEmpty;
                    if (haveHost && pageHost[idx] != BrickedVolume::kEmptySlot)
                        ++stats.visibleResident;
                    else
                        ++stats.visibleMissing;
                }
            }
        }
    }
    return stats;
}

} // namespace rendering
