#ifndef __EMSCRIPTEN__

#include "VolumeRenderer.hpp"
#include "src/utils/Logger.hpp"
#include "src/utils/FileUtils.hpp"

#include <cmath>
#include <cstring>

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

bool VolumeRenderer::uploadVolume(const std::vector<uint8_t>& density, uint32_t resolution) {
    const uint64_t bytes = static_cast<uint64_t>(resolution) * resolution * resolution;

    rhi::BufferDesc stagingDesc{};
    stagingDesc.size  = bytes;
    stagingDesc.usage = rhi::BufferUsage::CopySrc | rhi::BufferUsage::MapWrite;
    auto staging = m_device->createBuffer(stagingDesc);
    if (!staging) { LOG_ERROR("VolumeRenderer") << "staging buffer create failed"; return false; }
    {
        void* mapped = staging->map();
        std::memcpy(mapped, density.data(), bytes);
        staging->unmap();
    }

    rhi::TextureDesc texDesc{};
    texDesc.size          = rhi::Extent3D{resolution, resolution, resolution};
    texDesc.dimension     = rhi::TextureDimension::Texture3D;
    texDesc.format        = rhi::TextureFormat::R8Unorm;
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
    bufferCopy.bytesPerRow  = 0;   // tightly packed (Vulkan reads this as texels; 0 = packed)
    bufferCopy.rowsPerImage = 0;   // tightly packed slices

    rhi::TextureCopyInfo texCopy{};
    texCopy.texture  = m_volumeTexture.get();
    texCopy.mipLevel = 0;
    texCopy.origin   = {0, 0, 0};
    texCopy.aspect   = 0;

    encoder->copyBufferToTexture(bufferCopy, texCopy,
                                 rhi::Extent3D{resolution, resolution, resolution});

    encoder->transitionTextureLayout(m_volumeTexture.get(),
                                     rhi::TextureLayout::TransferDst,
                                     rhi::TextureLayout::ShaderReadOnly);

    auto cmd = encoder->finish();
    m_graphicsQueue->submit(cmd.get());
    m_graphicsQueue->waitIdle();  // one-shot upload; safe to stall here

    // 3D view.
    rhi::TextureViewDesc viewDesc{};
    viewDesc.format          = rhi::TextureFormat::R8Unorm;
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

    if (!uploadVolume(density, resolution)) return false;

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
    LOG_INFO("VolumeRenderer") << "initialized: " << resolution << "^3 R8 density volume";
    return true;
}

// ---------------------------------------------------------------------------
// Phase 7-3: ray-march pipeline + bind groups + UBO update
// ---------------------------------------------------------------------------

bool VolumeRenderer::createPipeline(rhi::RHITextureView* depthView, void* nativeRenderPass) {
    auto loadSpv = [&](const char* path, rhi::ShaderStage stage, const char* label) {
        auto raw = FileUtils::readFile(path);
        if (raw.empty()) { LOG_ERROR("VolumeRenderer") << "missing " << path; return std::unique_ptr<rhi::RHIShader>{}; }
        std::vector<uint8_t> code(raw.begin(), raw.end());
        rhi::ShaderSource src(rhi::ShaderLanguage::SPIRV, code, stage, "main");
        return m_device->createShader(rhi::ShaderDesc(src, label));
    };
    m_vertexShader   = loadSpv("shaders/volume_march.vert.spv", rhi::ShaderStage::Vertex,   "VolumeMarchVS");
    m_fragmentShader = loadSpv("shaders/volume_march.frag.spv", rhi::ShaderStage::Fragment, "VolumeMarchFS");
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
    layoutDesc.entries = {
        entry(0, S::Fragment, T::UniformBuffer),
        entry(1, S::Fragment, T::SampledTexture, D::View2D),  // scene depth
        entry(2, S::Fragment, T::Sampler),                    // depth sampler (nearest)
        entry(3, S::Fragment, T::SampledTexture, D::View3D),  // volume density
        entry(4, S::Fragment, T::Sampler),                    // volume sampler (linear)
    };
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

    // HDR target with PREMULTIPLIED-alpha over blending (shader outputs premult rgb).
    rhi::ColorTargetState ct;
    ct.format = rhi::TextureFormat::RGBA16Float;
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
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        rhi::BindGroupDesc desc;
        desc.layout = m_bindGroupLayout.get();
        desc.entries = {
            rhi::BindGroupEntry::Buffer(0, m_uniformBuffers[i].get(), 0, sizeof(VolumeUBO)),
            rhi::BindGroupEntry::TextureView(1, depthView),
            rhi::BindGroupEntry::Sampler(2, m_depthSampler.get()),
            rhi::BindGroupEntry::TextureView(3, m_volumeView.get()),
            rhi::BindGroupEntry::Sampler(4, m_sampler.get()),
        };
        desc.label = "VolumeBindGroup";
        m_bindGroups[i] = m_device->createBindGroup(desc);
        if (!m_bindGroups[i]) { LOG_ERROR("VolumeRenderer") << "bind group create failed"; return false; }
    }
    return true;
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
    ubo.tf        = glm::vec4(m_tfThreshold, m_tfColorMix, 0.0f, 0.0f);
    ubo.lowColor  = glm::vec4(m_lowColor, 1.0f);
    ubo.highColor = glm::vec4(m_highColor, 1.0f);

    m_uniformBuffers[fi]->write(&ubo, sizeof(VolumeUBO));
}

} // namespace rendering

#endif // !__EMSCRIPTEN__
