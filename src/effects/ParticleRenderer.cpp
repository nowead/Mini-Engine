#include "ParticleRenderer.hpp"
#include "src/utils/FileUtils.hpp"
#include <cstring>
#include <iostream>

namespace effects {

ParticleRenderer::ParticleRenderer(rhi::RHIDevice* device, rhi::RHIQueue* queue)
    : m_device(device)
    , m_queue(queue)
{
}

ParticleRenderer::~ParticleRenderer() = default;

bool ParticleRenderer::initialize(rhi::TextureFormat colorFormat, rhi::TextureFormat depthFormat,
                                   void* nativeRenderPass) {
    if (!createShaders()) {
        std::cerr << "[ParticleRenderer] Failed to create shaders\n";
        return false;
    }

    if (!createUniformBuffers()) {
        std::cerr << "[ParticleRenderer] Failed to create uniform buffers\n";
        return false;
    }

    if (!createBindGroups()) {
        std::cerr << "[ParticleRenderer] Failed to create bind groups\n";
        return false;
    }

    if (!createPipeline(colorFormat, depthFormat, nativeRenderPass)) {
        std::cerr << "[ParticleRenderer] Failed to create pipeline\n";
        return false;
    }

    return true;
}

bool ParticleRenderer::createShaders() {
    // Load vertex shader
    auto vertCodeRaw = FileUtils::readFile("shaders/particle.vert.spv");
    if (vertCodeRaw.empty()) {
        std::cerr << "[ParticleRenderer] Failed to load particle.vert.spv\n";
        return false;
    }
    std::vector<uint8_t> vertCode(vertCodeRaw.begin(), vertCodeRaw.end());

    rhi::ShaderSource vertSource(rhi::ShaderLanguage::SPIRV, vertCode, rhi::ShaderStage::Vertex, "main");
    rhi::ShaderDesc vertDesc(vertSource, "ParticleVertexShader");

    m_vertexShader = m_device->createShader(vertDesc);
    if (!m_vertexShader) return false;

    // Load fragment shader
    auto fragCodeRaw = FileUtils::readFile("shaders/particle.frag.spv");
    if (fragCodeRaw.empty()) {
        std::cerr << "[ParticleRenderer] Failed to load particle.frag.spv\n";
        return false;
    }
    std::vector<uint8_t> fragCode(fragCodeRaw.begin(), fragCodeRaw.end());

    rhi::ShaderSource fragSource(rhi::ShaderLanguage::SPIRV, fragCode, rhi::ShaderStage::Fragment, "main");
    rhi::ShaderDesc fragDesc(fragSource, "ParticleFragmentShader");

    m_fragmentShader = m_device->createShader(fragDesc);
    return m_fragmentShader != nullptr;
}

bool ParticleRenderer::createUniformBuffers() {
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        rhi::BufferDesc desc;
        desc.size = sizeof(UniformData);
        desc.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::MapWrite;
        desc.mappedAtCreation = false;
        desc.label = "ParticleUniformBuffer";

        m_uniformBuffers[i] = m_device->createBuffer(desc);
        if (!m_uniformBuffers[i]) return false;
    }
    return true;
}

bool ParticleRenderer::createBindGroups() {
    // Create bind group layout
    rhi::BindGroupLayoutDesc layoutDesc;
    layoutDesc.entries.push_back(rhi::BindGroupLayoutEntry(0, rhi::ShaderStage::Vertex, rhi::BindingType::UniformBuffer));
    layoutDesc.label = "ParticleBindGroupLayout";

    m_bindGroupLayout = m_device->createBindGroupLayout(layoutDesc);
    if (!m_bindGroupLayout) return false;

    // Create bind groups for each frame
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        rhi::BindGroupDesc groupDesc;
        groupDesc.layout = m_bindGroupLayout.get();
        groupDesc.entries.push_back(rhi::BindGroupEntry::Buffer(0, m_uniformBuffers[i].get(), 0, sizeof(UniformData)));
        groupDesc.label = "ParticleBindGroup";

        m_bindGroups[i] = m_device->createBindGroup(groupDesc);
        if (!m_bindGroups[i]) return false;
    }

    return true;
}

bool ParticleRenderer::createPipeline(rhi::TextureFormat colorFormat, rhi::TextureFormat depthFormat,
                                       void* nativeRenderPass) {
    // Create pipeline layout
    rhi::PipelineLayoutDesc layoutDesc;
    layoutDesc.bindGroupLayouts.push_back(m_bindGroupLayout.get());
    layoutDesc.label = "ParticlePipelineLayout";

    m_pipelineLayout = m_device->createPipelineLayout(layoutDesc);
    if (!m_pipelineLayout) return false;

    // Create render pipeline
    rhi::RenderPipelineDesc pipelineDesc;
    pipelineDesc.label = "ParticlePipeline";
    pipelineDesc.layout = m_pipelineLayout.get();

    // Shaders
    pipelineDesc.vertexShader = m_vertexShader.get();
    pipelineDesc.fragmentShader = m_fragmentShader.get();

    // Vertex attributes for particle data (instanced)
    // Particle struct: 64 bytes total
    rhi::VertexBufferLayout vertexLayout;
    vertexLayout.stride = 64;  // sizeof(Particle)
    vertexLayout.inputRate = rhi::VertexInputRate::Instance;  // Instanced
    vertexLayout.attributes = {
        rhi::VertexAttribute(0, 0, rhi::TextureFormat::RGB32Float, 0),    // position (offset 0)
        rhi::VertexAttribute(1, 0, rhi::TextureFormat::R32Float,   12),   // lifetime (offset 12)
        rhi::VertexAttribute(2, 0, rhi::TextureFormat::RGB32Float, 16),   // velocity (offset 16)
        rhi::VertexAttribute(3, 0, rhi::TextureFormat::R32Float,   28),   // age (offset 28)
        rhi::VertexAttribute(4, 0, rhi::TextureFormat::RGBA32Float, 32),  // color (offset 32)
        rhi::VertexAttribute(5, 0, rhi::TextureFormat::RG32Float,  48),   // size (offset 48)
        rhi::VertexAttribute(6, 0, rhi::TextureFormat::R32Float,   56),   // rotation (offset 56)
        rhi::VertexAttribute(7, 0, rhi::TextureFormat::R32Float,   60),   // rotationSpeed (offset 60)
    };
    pipelineDesc.vertex.buffers.push_back(vertexLayout);

    // Primitive state
    pipelineDesc.primitive.topology = rhi::PrimitiveTopology::TriangleList;
    pipelineDesc.primitive.cullMode = rhi::CullMode::None;  // Billboards face camera
    pipelineDesc.primitive.frontFace = rhi::FrontFace::CounterClockwise;

    // Depth state - read but don't write (particles are transparent)
    rhi::DepthStencilState depthStencilState;
    depthStencilState.depthTestEnabled = true;
    depthStencilState.depthWriteEnabled = false;
    depthStencilState.depthCompare = rhi::CompareOp::Less;
    depthStencilState.format = depthFormat;
    pipelineDesc.depthStencil = &depthStencilState;

    // Color target with blending
    rhi::ColorTargetState colorTarget;
    colorTarget.format = colorFormat;
    colorTarget.blend.blendEnabled = true;

    if (m_blendMode == BlendMode::Additive) {
        // Additive blending: src + dst
        colorTarget.blend.srcColorFactor = rhi::BlendFactor::SrcAlpha;
        colorTarget.blend.dstColorFactor = rhi::BlendFactor::One;
        colorTarget.blend.colorBlendOp = rhi::BlendOp::Add;
        colorTarget.blend.srcAlphaFactor = rhi::BlendFactor::One;
        colorTarget.blend.dstAlphaFactor = rhi::BlendFactor::One;
        colorTarget.blend.alphaBlendOp = rhi::BlendOp::Add;
    } else {
        // Alpha blending: src * srcAlpha + dst * (1 - srcAlpha)
        colorTarget.blend.srcColorFactor = rhi::BlendFactor::SrcAlpha;
        colorTarget.blend.dstColorFactor = rhi::BlendFactor::OneMinusSrcAlpha;
        colorTarget.blend.colorBlendOp = rhi::BlendOp::Add;
        colorTarget.blend.srcAlphaFactor = rhi::BlendFactor::One;
        colorTarget.blend.dstAlphaFactor = rhi::BlendFactor::OneMinusSrcAlpha;
        colorTarget.blend.alphaBlendOp = rhi::BlendOp::Add;
    }

    pipelineDesc.colorTargets.push_back(colorTarget);

    // Linux needs native render pass for pipeline creation
    if (nativeRenderPass) {
        pipelineDesc.nativeRenderPass = nativeRenderPass;
    }

    m_pipeline = m_device->createRenderPipeline(pipelineDesc);
    return m_pipeline != nullptr;
}

void ParticleRenderer::updateCamera(const glm::mat4& view, const glm::mat4& projection) {
    m_viewMatrix = view;
    m_projMatrix = projection;
}

void ParticleRenderer::render(rhi::RHIRenderPassEncoder* encoder,
                              ParticleSystem& particleSystem,
                              uint32_t frameIndex) {
    if (!encoder || !m_pipeline) return;

    // Upload particles to GPU
    particleSystem.uploadToGPU();

    rhi::RHIBuffer* particleBuffer = particleSystem.getParticleBuffer();
    uint32_t particleCount = particleSystem.getTotalActiveParticles();

    if (!particleBuffer || particleCount == 0) return;

    // Update uniform buffer
    UniformData ubo;
    ubo.model = glm::mat4(1.0f);  // Identity model matrix
    ubo.view = m_viewMatrix;
    ubo.proj = m_projMatrix;

    void* mapped = m_uniformBuffers[frameIndex]->map();
    if (mapped) {
        std::memcpy(mapped, &ubo, sizeof(ubo));
        m_uniformBuffers[frameIndex]->unmap();
    }

    // Set pipeline and bind group
    encoder->setPipeline(m_pipeline.get());
    encoder->setBindGroup(0, m_bindGroups[frameIndex].get(), {});

    // Bind particle buffer as vertex buffer (instanced)
    encoder->setVertexBuffer(0, particleBuffer, 0);

    // Draw particles (6 vertices per particle for quad)
    encoder->draw(6, particleCount, 0, 0);
}

void ParticleRenderer::setBlendMode(BlendMode mode) {
    m_blendMode = mode;
    // Note: Pipeline needs to be recreated to apply new blend mode
}

} // namespace effects
