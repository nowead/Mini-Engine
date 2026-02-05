#include "IBLManager.hpp"
#include "src/utils/FileUtils.hpp"
#include <iostream>
#include <cstring>

namespace rendering {

IBLManager::IBLManager(rhi::RHIDevice* device, rhi::RHIQueue* queue)
    : m_device(device), m_queue(queue) {
}

bool IBLManager::initialize(rhi::RHITexture* hdrTexture) {
    if (!m_device || !m_queue) {
        std::cerr << "[IBLManager] Invalid device or queue\n";
        return false;
    }

    if (!hdrTexture) {
        std::cerr << "[IBLManager] No HDR texture provided, using default\n";
        return initializeDefault();
    }

    if (!createTextures()) {
        std::cerr << "[IBLManager] Failed to create IBL textures\n";
        return false;
    }

    if (!createSampler()) {
        std::cerr << "[IBLManager] Failed to create sampler\n";
        return false;
    }

    // Pass 1: BRDF LUT (no input dependency)
    if (!generateBRDFLut()) {
        std::cerr << "[IBLManager] Failed to generate BRDF LUT\n";
        return false;
    }

    // Pass 2: Equirect → Cubemap
    if (!generateEnvCubemap(hdrTexture)) {
        std::cerr << "[IBLManager] Failed to generate environment cubemap\n";
        return false;
    }

    // Pass 3: Irradiance Map (from env cubemap)
    if (!generateIrradianceMap()) {
        std::cerr << "[IBLManager] Failed to generate irradiance map\n";
        return false;
    }

    // Pass 4: Prefiltered Env Map (from env cubemap)
    if (!generatePrefilteredMap()) {
        std::cerr << "[IBLManager] Failed to generate prefiltered map\n";
        return false;
    }

    m_initialized = true;
    std::cout << "[IBLManager] IBL initialization complete\n";
    return true;
}

bool IBLManager::initializeDefault() {
    if (!createTextures()) {
        std::cerr << "[IBLManager] Failed to create IBL textures\n";
        return false;
    }

    if (!createSampler()) {
        std::cerr << "[IBLManager] Failed to create sampler\n";
        return false;
    }

    // Generate BRDF LUT (always needed, no HDR dependency)
    if (!generateBRDFLut()) {
        std::cerr << "[IBLManager] Failed to generate BRDF LUT\n";
        return false;
    }

    // Transition all cubemap textures to ShaderReadOnly (even though empty)
    // This prevents Vulkan validation errors when they're bound as sampled textures
    {
        auto encoder = m_device->createCommandEncoder();
        encoder->transitionTextureLayout(m_envCubemap.get(),
                                         rhi::TextureLayout::Undefined,
                                         rhi::TextureLayout::ShaderReadOnly);
        encoder->transitionTextureLayout(m_irradianceMap.get(),
                                         rhi::TextureLayout::Undefined,
                                         rhi::TextureLayout::ShaderReadOnly);
        encoder->transitionTextureLayout(m_prefilteredMap.get(),
                                         rhi::TextureLayout::Undefined,
                                         rhi::TextureLayout::ShaderReadOnly);
        auto cmdBuffer = encoder->finish();
        m_queue->submit(cmdBuffer.get());
        m_queue->waitIdle();
    }

    m_initialized = true;
    std::cout << "[IBLManager] IBL default initialization complete (BRDF LUT only)\n";
    return true;
}

// =============================================================================
// Texture Creation
// =============================================================================

bool IBLManager::createTextures() {
    // Environment Cubemap: 512x512x6, RGBA16Float
    {
        rhi::TextureDesc desc{};
        desc.size = {512, 512, 1};
        desc.format = rhi::TextureFormat::RGBA16Float;
        desc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
        desc.mipLevelCount = 1;
        desc.arrayLayerCount = 6;
        desc.isCubemap = true;
        desc.label = "IBL_EnvCubemap";

        m_envCubemap = m_device->createTexture(desc);
        if (!m_envCubemap) return false;
        m_envCubemapView = m_envCubemap->createDefaultView();
    }

    // Irradiance Map: 32x32x6, RGBA16Float
    {
        rhi::TextureDesc desc{};
        desc.size = {32, 32, 1};
        desc.format = rhi::TextureFormat::RGBA16Float;
        desc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
        desc.mipLevelCount = 1;
        desc.arrayLayerCount = 6;
        desc.isCubemap = true;
        desc.label = "IBL_IrradianceMap";

        m_irradianceMap = m_device->createTexture(desc);
        if (!m_irradianceMap) return false;
        m_irradianceView = m_irradianceMap->createDefaultView();
    }

    // Prefiltered Environment Map: 128x128x6, RGBA16Float, 5 mip levels
    {
        rhi::TextureDesc desc{};
        desc.size = {128, 128, 1};
        desc.format = rhi::TextureFormat::RGBA16Float;
        desc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
        desc.mipLevelCount = 5;  // roughness levels: 0.0, 0.25, 0.5, 0.75, 1.0
        desc.arrayLayerCount = 6;
        desc.isCubemap = true;
        desc.label = "IBL_PrefilteredMap";

        m_prefilteredMap = m_device->createTexture(desc);
        if (!m_prefilteredMap) return false;
        m_prefilteredView = m_prefilteredMap->createDefaultView();
    }

    // BRDF LUT: 512x512, RG16Float
    {
        rhi::TextureDesc desc{};
        desc.size = {512, 512, 1};
        desc.format = rhi::TextureFormat::RG16Float;
        desc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
        desc.mipLevelCount = 1;
        desc.label = "IBL_BRDF_LUT";

        m_brdfLut = m_device->createTexture(desc);
        if (!m_brdfLut) return false;
        m_brdfLutView = m_brdfLut->createDefaultView();
    }

    std::cout << "[IBLManager] Created IBL textures\n";
    return true;
}

bool IBLManager::createSampler() {
    rhi::SamplerDesc desc{};
    desc.magFilter = rhi::FilterMode::Linear;
    desc.minFilter = rhi::FilterMode::Linear;
    desc.mipmapFilter = rhi::MipmapMode::Linear;
    desc.addressModeU = rhi::AddressMode::ClampToEdge;
    desc.addressModeV = rhi::AddressMode::ClampToEdge;
    desc.addressModeW = rhi::AddressMode::ClampToEdge;
    desc.maxAnisotropy = 1;
    desc.label = "IBL_Sampler";

    m_sampler = m_device->createSampler(desc);
    return m_sampler != nullptr;
}

// =============================================================================
// Shader Loading
// =============================================================================

std::unique_ptr<rhi::RHIShader> IBLManager::loadComputeShader(const std::string& name) {
#ifdef __EMSCRIPTEN__
    std::string path = "shaders/" + name + ".wgsl";
    auto codeRaw = FileUtils::readFile(path);
    if (codeRaw.empty()) {
        std::cerr << "[IBLManager] Failed to load " << path << "\n";
        return nullptr;
    }
    std::vector<uint8_t> code(codeRaw.begin(), codeRaw.end());
    rhi::ShaderSource source(rhi::ShaderLanguage::WGSL, code, rhi::ShaderStage::Compute, "main");
#else
    std::string path = "shaders/" + name + ".comp.spv";
    auto codeRaw = FileUtils::readFile(path);
    if (codeRaw.empty()) {
        std::cerr << "[IBLManager] Failed to load " << path << "\n";
        return nullptr;
    }
    std::vector<uint8_t> code(codeRaw.begin(), codeRaw.end());
    rhi::ShaderSource source(rhi::ShaderLanguage::SPIRV, code, rhi::ShaderStage::Compute, "main");
#endif

    rhi::ShaderDesc desc(source, name.c_str());
    return m_device->createShader(desc);
}

// =============================================================================
// Compute Passes (to be implemented with shader creation)
// =============================================================================

bool IBLManager::generateBRDFLut() {
    auto shader = loadComputeShader("brdf_lut");
    if (!shader) {
        std::cerr << "[IBLManager] BRDF LUT shader not found (will be added in Step 4A)\n";
        return true;  // Non-fatal: allow skeleton to build
    }

    // Create bind group layout: binding 0 = storage texture (write-only RG16Float)
    rhi::BindGroupLayoutDesc layoutDesc{};
    rhi::BindGroupLayoutEntry entry{};
    entry.binding = 0;
    entry.visibility = rhi::ShaderStage::Compute;
    entry.type = rhi::BindingType::StorageTexture;
    entry.storageTextureFormat = rhi::TextureFormat::RG16Float;
    layoutDesc.entries = {entry};
    layoutDesc.label = "BRDF_LUT_BindGroupLayout";

    auto bindGroupLayout = m_device->createBindGroupLayout(layoutDesc);
    if (!bindGroupLayout) return false;

    // Create bind group
    rhi::BindGroupDesc bgDesc{};
    bgDesc.layout = bindGroupLayout.get();
    bgDesc.entries = {rhi::BindGroupEntry::TextureView(0, m_brdfLutView.get())};
    bgDesc.label = "BRDF_LUT_BindGroup";

    auto bindGroup = m_device->createBindGroup(bgDesc);
    if (!bindGroup) return false;

    // Create pipeline layout
    rhi::PipelineLayoutDesc plDesc{};
    plDesc.bindGroupLayouts = {bindGroupLayout.get()};
    auto pipelineLayout = m_device->createPipelineLayout(plDesc);
    if (!pipelineLayout) return false;

    // Create compute pipeline
    rhi::ComputePipelineDesc cpDesc(shader.get(), pipelineLayout.get());
    cpDesc.label = "BRDF_LUT_Pipeline";
    auto pipeline = m_device->createComputePipeline(cpDesc);
    if (!pipeline) return false;

    // Dispatch compute: 512/16 = 32 workgroups per dimension
    auto encoder = m_device->createCommandEncoder();

    encoder->transitionTextureLayout(m_brdfLut.get(),
                                     rhi::TextureLayout::Undefined,
                                     rhi::TextureLayout::General);

    auto computePass = encoder->beginComputePass("BRDF_LUT");
    computePass->setPipeline(pipeline.get());
    computePass->setBindGroup(0, bindGroup.get());
    computePass->dispatch(512 / 16, 512 / 16, 1);
    computePass->end();

    encoder->transitionTextureLayout(m_brdfLut.get(),
                                     rhi::TextureLayout::General,
                                     rhi::TextureLayout::ShaderReadOnly);

    auto cmdBuffer = encoder->finish();
    m_queue->submit(cmdBuffer.get());
    m_queue->waitIdle();

    std::cout << "[IBLManager] Generated BRDF LUT (512x512)\n";
    return true;
}

bool IBLManager::generateEnvCubemap(rhi::RHITexture* hdrTexture) {
    auto shader = loadComputeShader("equirect_to_cubemap");
    if (!shader) {
        std::cerr << "[IBLManager] Equirect shader not found (will be added in Step 4B)\n";
        return true;  // Non-fatal
    }

    // Create a view of the HDR equirect texture
    auto hdrView = hdrTexture->createDefaultView();

    // Create bind group layout:
    // binding 0 = sampled texture (equirect HDR)
    // binding 1 = sampler
    // binding 2 = storage texture (cubemap output, 2D array for compute write)
    rhi::BindGroupLayoutDesc layoutDesc{};
    {
        rhi::BindGroupLayoutEntry e{};
        e.binding = 0;
        e.visibility = rhi::ShaderStage::Compute;
        e.type = rhi::BindingType::SampledTexture;
        layoutDesc.entries.push_back(e);
    }
    {
        rhi::BindGroupLayoutEntry e{};
        e.binding = 1;
        e.visibility = rhi::ShaderStage::Compute;
        e.type = rhi::BindingType::Sampler;
        layoutDesc.entries.push_back(e);
    }
    {
        rhi::BindGroupLayoutEntry e{};
        e.binding = 2;
        e.visibility = rhi::ShaderStage::Compute;
        e.type = rhi::BindingType::StorageTexture;
        e.storageTextureFormat = rhi::TextureFormat::RGBA16Float;
        e.textureViewDimension = rhi::TextureViewDimension::View2DArray;
        layoutDesc.entries.push_back(e);
    }
    layoutDesc.label = "EnvCubemap_BindGroupLayout";

    auto bindGroupLayout = m_device->createBindGroupLayout(layoutDesc);
    if (!bindGroupLayout) return false;

    // Create 2D array view of cubemap for storage write
    rhi::TextureViewDesc arrayViewDesc{};
    arrayViewDesc.dimension = rhi::TextureViewDimension::View2DArray;
    arrayViewDesc.format = rhi::TextureFormat::RGBA16Float;
    arrayViewDesc.baseArrayLayer = 0;
    arrayViewDesc.arrayLayerCount = 6;
    arrayViewDesc.baseMipLevel = 0;
    arrayViewDesc.mipLevelCount = 1;
    auto envArrayView = m_envCubemap->createView(arrayViewDesc);

    rhi::BindGroupDesc bgDesc{};
    bgDesc.layout = bindGroupLayout.get();
    bgDesc.entries = {
        rhi::BindGroupEntry::TextureView(0, hdrView.get()),
        rhi::BindGroupEntry::Sampler(1, m_sampler.get()),
        rhi::BindGroupEntry::TextureView(2, envArrayView.get()),
    };
    auto bindGroup = m_device->createBindGroup(bgDesc);
    if (!bindGroup) return false;

    rhi::PipelineLayoutDesc plDesc{};
    plDesc.bindGroupLayouts = {bindGroupLayout.get()};
    auto pipelineLayout = m_device->createPipelineLayout(plDesc);

    rhi::ComputePipelineDesc cpDesc(shader.get(), pipelineLayout.get());
    cpDesc.label = "EnvCubemap_Pipeline";
    auto pipeline = m_device->createComputePipeline(cpDesc);
    if (!pipeline) return false;

    // Dispatch: 512/16=32 per XY, 6 faces
    auto encoder = m_device->createCommandEncoder();

    encoder->transitionTextureLayout(m_envCubemap.get(),
                                     rhi::TextureLayout::Undefined,
                                     rhi::TextureLayout::General);

    auto computePass = encoder->beginComputePass("EquirectToCubemap");
    computePass->setPipeline(pipeline.get());
    computePass->setBindGroup(0, bindGroup.get());
    computePass->dispatch(512 / 16, 512 / 16, 6);
    computePass->end();

    encoder->transitionTextureLayout(m_envCubemap.get(),
                                     rhi::TextureLayout::General,
                                     rhi::TextureLayout::ShaderReadOnly);

    auto cmdBuffer = encoder->finish();
    m_queue->submit(cmdBuffer.get());
    m_queue->waitIdle();

    std::cout << "[IBLManager] Generated environment cubemap (512x512x6)\n";
    return true;
}

bool IBLManager::generateIrradianceMap() {
    auto shader = loadComputeShader("irradiance_map");
    if (!shader) {
        std::cerr << "[IBLManager] Irradiance shader not found (will be added in Step 4C)\n";
        return true;  // Non-fatal
    }

    // binding 0 = sampled cubemap (env)
    // binding 1 = sampler
    // binding 2 = storage texture (irradiance output, 2D array)
    rhi::BindGroupLayoutDesc layoutDesc{};
    {
        rhi::BindGroupLayoutEntry e{};
        e.binding = 0;
        e.visibility = rhi::ShaderStage::Compute;
        e.type = rhi::BindingType::SampledTexture;
        e.textureViewDimension = rhi::TextureViewDimension::ViewCube;
        layoutDesc.entries.push_back(e);
    }
    {
        rhi::BindGroupLayoutEntry e{};
        e.binding = 1;
        e.visibility = rhi::ShaderStage::Compute;
        e.type = rhi::BindingType::Sampler;
        layoutDesc.entries.push_back(e);
    }
    {
        rhi::BindGroupLayoutEntry e{};
        e.binding = 2;
        e.visibility = rhi::ShaderStage::Compute;
        e.type = rhi::BindingType::StorageTexture;
        e.storageTextureFormat = rhi::TextureFormat::RGBA16Float;
        e.textureViewDimension = rhi::TextureViewDimension::View2DArray;
        layoutDesc.entries.push_back(e);
    }
    layoutDesc.label = "Irradiance_BindGroupLayout";

    auto bindGroupLayout = m_device->createBindGroupLayout(layoutDesc);
    if (!bindGroupLayout) return false;

    // Create 2D array view of irradiance for storage write
    rhi::TextureViewDesc arrayViewDesc{};
    arrayViewDesc.dimension = rhi::TextureViewDimension::View2DArray;
    arrayViewDesc.format = rhi::TextureFormat::RGBA16Float;
    arrayViewDesc.baseArrayLayer = 0;
    arrayViewDesc.arrayLayerCount = 6;
    auto irrArrayView = m_irradianceMap->createView(arrayViewDesc);

    rhi::BindGroupDesc bgDesc{};
    bgDesc.layout = bindGroupLayout.get();
    bgDesc.entries = {
        rhi::BindGroupEntry::TextureView(0, m_envCubemapView.get()),
        rhi::BindGroupEntry::Sampler(1, m_sampler.get()),
        rhi::BindGroupEntry::TextureView(2, irrArrayView.get()),
    };
    auto bindGroup = m_device->createBindGroup(bgDesc);
    if (!bindGroup) return false;

    rhi::PipelineLayoutDesc plDesc{};
    plDesc.bindGroupLayouts = {bindGroupLayout.get()};
    auto pipelineLayout = m_device->createPipelineLayout(plDesc);

    rhi::ComputePipelineDesc cpDesc(shader.get(), pipelineLayout.get());
    cpDesc.label = "Irradiance_Pipeline";
    auto pipeline = m_device->createComputePipeline(cpDesc);
    if (!pipeline) return false;

    // Dispatch: 32/16=2 per XY, 6 faces
    auto encoder = m_device->createCommandEncoder();

    encoder->transitionTextureLayout(m_irradianceMap.get(),
                                     rhi::TextureLayout::Undefined,
                                     rhi::TextureLayout::General);

    auto computePass = encoder->beginComputePass("IrradianceMap");
    computePass->setPipeline(pipeline.get());
    computePass->setBindGroup(0, bindGroup.get());
    computePass->dispatch(32 / 16, 32 / 16, 6);
    computePass->end();

    encoder->transitionTextureLayout(m_irradianceMap.get(),
                                     rhi::TextureLayout::General,
                                     rhi::TextureLayout::ShaderReadOnly);

    auto cmdBuffer = encoder->finish();
    m_queue->submit(cmdBuffer.get());
    m_queue->waitIdle();

    std::cout << "[IBLManager] Generated irradiance map (32x32x6)\n";
    return true;
}

bool IBLManager::generatePrefilteredMap() {
    auto shader = loadComputeShader("prefilter_env");
    if (!shader) {
        std::cerr << "[IBLManager] Prefilter shader not found (will be added in Step 4D)\n";
        return true;  // Non-fatal
    }

    // binding 0 = sampled cubemap (env)
    // binding 1 = sampler
    // binding 2 = storage texture (prefiltered output mip, 2D array)
    // binding 3 = uniform buffer (roughness)
    rhi::BindGroupLayoutDesc layoutDesc{};
    {
        rhi::BindGroupLayoutEntry e{};
        e.binding = 0;
        e.visibility = rhi::ShaderStage::Compute;
        e.type = rhi::BindingType::SampledTexture;
        e.textureViewDimension = rhi::TextureViewDimension::ViewCube;
        layoutDesc.entries.push_back(e);
    }
    {
        rhi::BindGroupLayoutEntry e{};
        e.binding = 1;
        e.visibility = rhi::ShaderStage::Compute;
        e.type = rhi::BindingType::Sampler;
        layoutDesc.entries.push_back(e);
    }
    {
        rhi::BindGroupLayoutEntry e{};
        e.binding = 2;
        e.visibility = rhi::ShaderStage::Compute;
        e.type = rhi::BindingType::StorageTexture;
        e.storageTextureFormat = rhi::TextureFormat::RGBA16Float;
        e.textureViewDimension = rhi::TextureViewDimension::View2DArray;
        layoutDesc.entries.push_back(e);
    }
    {
        rhi::BindGroupLayoutEntry e{};
        e.binding = 3;
        e.visibility = rhi::ShaderStage::Compute;
        e.type = rhi::BindingType::UniformBuffer;
        layoutDesc.entries.push_back(e);
    }
    layoutDesc.label = "Prefilter_BindGroupLayout";

    auto bindGroupLayout = m_device->createBindGroupLayout(layoutDesc);
    if (!bindGroupLayout) return false;

    rhi::PipelineLayoutDesc plDesc{};
    plDesc.bindGroupLayouts = {bindGroupLayout.get()};
    auto pipelineLayout = m_device->createPipelineLayout(plDesc);

    rhi::ComputePipelineDesc cpDesc(shader.get(), pipelineLayout.get());
    cpDesc.label = "Prefilter_Pipeline";
    auto pipeline = m_device->createComputePipeline(cpDesc);
    if (!pipeline) return false;

    // Create roughness UBO
    rhi::BufferDesc uboDesc{};
    uboDesc.size = 16;  // vec4 alignment: float roughness + padding
    uboDesc.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::MapWrite;
    auto roughnessUBO = m_device->createBuffer(uboDesc);

    auto encoder = m_device->createCommandEncoder();

    encoder->transitionTextureLayout(m_prefilteredMap.get(),
                                     rhi::TextureLayout::Undefined,
                                     rhi::TextureLayout::General);

    // Dispatch once per mip level (5 roughness levels)
    for (uint32_t mip = 0; mip < 5; mip++) {
        float roughness = static_cast<float>(mip) / 4.0f;
        uint32_t mipSize = 128 >> mip;

        // Update roughness UBO
        void* mapped = roughnessUBO->map();
        std::memcpy(mapped, &roughness, sizeof(float));
        roughnessUBO->unmap();

        // Create per-mip view for storage write
        rhi::TextureViewDesc mipViewDesc{};
        mipViewDesc.dimension = rhi::TextureViewDimension::View2DArray;
        mipViewDesc.format = rhi::TextureFormat::RGBA16Float;
        mipViewDesc.baseMipLevel = mip;
        mipViewDesc.mipLevelCount = 1;
        mipViewDesc.baseArrayLayer = 0;
        mipViewDesc.arrayLayerCount = 6;
        auto mipView = m_prefilteredMap->createView(mipViewDesc);

        rhi::BindGroupDesc bgDesc{};
        bgDesc.layout = bindGroupLayout.get();
        bgDesc.entries = {
            rhi::BindGroupEntry::TextureView(0, m_envCubemapView.get()),
            rhi::BindGroupEntry::Sampler(1, m_sampler.get()),
            rhi::BindGroupEntry::TextureView(2, mipView.get()),
            rhi::BindGroupEntry::Buffer(3, roughnessUBO.get()),
        };
        auto bindGroup = m_device->createBindGroup(bgDesc);

        uint32_t workgroups = std::max(1u, mipSize / 16);
        auto computePass = encoder->beginComputePass("PrefilterMip");
        computePass->setPipeline(pipeline.get());
        computePass->setBindGroup(0, bindGroup.get());
        computePass->dispatch(workgroups, workgroups, 6);
        computePass->end();
    }

    encoder->transitionTextureLayout(m_prefilteredMap.get(),
                                     rhi::TextureLayout::General,
                                     rhi::TextureLayout::ShaderReadOnly);

    auto cmdBuffer = encoder->finish();
    m_queue->submit(cmdBuffer.get());
    m_queue->waitIdle();

    std::cout << "[IBLManager] Generated prefiltered env map (128x128x6, 5 mips)\n";
    return true;
}

} // namespace rendering
