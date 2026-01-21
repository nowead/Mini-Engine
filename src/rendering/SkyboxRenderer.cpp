#include "SkyboxRenderer.hpp"
#include "src/utils/FileUtils.hpp"
#include <cstring>
#include <iostream>

namespace rendering {

SkyboxRenderer::SkyboxRenderer(rhi::RHIDevice* device, rhi::RHIQueue* queue)
    : m_device(device), m_queue(queue) {
}

bool SkyboxRenderer::initialize(rhi::TextureFormat colorFormat, rhi::TextureFormat depthFormat,
                                 void* nativeRenderPass) {
    if (!m_device || !m_queue) {
        std::cerr << "[SkyboxRenderer] Invalid device or queue\n";
        return false;
    }

    if (!createShaders()) {
        std::cerr << "[SkyboxRenderer] Failed to create shaders\n";
        return false;
    }

    if (!createUniformBuffers()) {
        std::cerr << "[SkyboxRenderer] Failed to create uniform buffers\n";
        return false;
    }

    if (!createBindGroups()) {
        std::cerr << "[SkyboxRenderer] Failed to create bind groups\n";
        return false;
    }

    if (!createPipeline(colorFormat, depthFormat, nativeRenderPass)) {
        std::cerr << "[SkyboxRenderer] Failed to create pipeline\n";
        return false;
    }

    std::cout << "[SkyboxRenderer] Initialized successfully\n";
    return true;
}

bool SkyboxRenderer::createShaders() {
    // Load vertex shader (pre-compiled SPIR-V)
    auto vertCodeRaw = FileUtils::readFile("shaders/skybox.vert.spv");
    if (vertCodeRaw.empty()) {
        std::cerr << "[SkyboxRenderer] Failed to load skybox.vert.spv\n";
        return false;
    }
    std::vector<uint8_t> vertCode(vertCodeRaw.begin(), vertCodeRaw.end());

    rhi::ShaderSource vertSource(rhi::ShaderLanguage::SPIRV, vertCode, rhi::ShaderStage::Vertex, "main");
    rhi::ShaderDesc vertDesc(vertSource, "SkyboxVertexShader");

    m_vertexShader = m_device->createShader(vertDesc);
    if (!m_vertexShader) {
        std::cerr << "[SkyboxRenderer] Failed to create vertex shader\n";
        return false;
    }

    // Load fragment shader (pre-compiled SPIR-V)
    auto fragCodeRaw = FileUtils::readFile("shaders/skybox.frag.spv");
    if (fragCodeRaw.empty()) {
        std::cerr << "[SkyboxRenderer] Failed to load skybox.frag.spv\n";
        return false;
    }
    std::vector<uint8_t> fragCode(fragCodeRaw.begin(), fragCodeRaw.end());

    rhi::ShaderSource fragSource(rhi::ShaderLanguage::SPIRV, fragCode, rhi::ShaderStage::Fragment, "main");
    rhi::ShaderDesc fragDesc(fragSource, "SkyboxFragmentShader");

    m_fragmentShader = m_device->createShader(fragDesc);
    if (!m_fragmentShader) {
        std::cerr << "[SkyboxRenderer] Failed to create fragment shader\n";
        return false;
    }

    std::cout << "[SkyboxRenderer] Shaders created successfully\n";
    return true;
}

bool SkyboxRenderer::createUniformBuffers() {
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        rhi::BufferDesc desc;
        desc.size = sizeof(UniformData);
        desc.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::MapWrite;
        desc.mappedAtCreation = false;
        desc.label = "SkyboxUniformBuffer";

        m_uniformBuffers[i] = m_device->createBuffer(desc);
        if (!m_uniformBuffers[i]) {
            std::cerr << "[SkyboxRenderer] Failed to create uniform buffer " << i << "\n";
            return false;
        }
    }
    return true;
}

bool SkyboxRenderer::createBindGroups() {
    // Create bind group layout
    rhi::BindGroupLayoutDesc layoutDesc;
    layoutDesc.entries.push_back(rhi::BindGroupLayoutEntry(0, rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, rhi::BindingType::UniformBuffer));
    layoutDesc.label = "SkyboxBindGroupLayout";

    m_bindGroupLayout = m_device->createBindGroupLayout(layoutDesc);
    if (!m_bindGroupLayout) {
        std::cerr << "[SkyboxRenderer] Failed to create bind group layout\n";
        return false;
    }

    // Create bind groups for each frame
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        rhi::BindGroupDesc groupDesc;
        groupDesc.layout = m_bindGroupLayout.get();
        groupDesc.entries.push_back(rhi::BindGroupEntry::Buffer(0, m_uniformBuffers[i].get(), 0, sizeof(UniformData)));
        groupDesc.label = "SkyboxBindGroup";

        m_bindGroups[i] = m_device->createBindGroup(groupDesc);
        if (!m_bindGroups[i]) {
            std::cerr << "[SkyboxRenderer] Failed to create bind group " << i << "\n";
            return false;
        }
    }

    return true;
}

bool SkyboxRenderer::createPipeline(rhi::TextureFormat colorFormat, rhi::TextureFormat depthFormat,
                                     void* nativeRenderPass) {
    // Create pipeline layout
    rhi::PipelineLayoutDesc layoutDesc;
    layoutDesc.bindGroupLayouts.push_back(m_bindGroupLayout.get());
    layoutDesc.label = "SkyboxPipelineLayout";

    m_pipelineLayout = m_device->createPipelineLayout(layoutDesc);
    if (!m_pipelineLayout) {
        std::cerr << "[SkyboxRenderer] Failed to create pipeline layout\n";
        return false;
    }

    // Create render pipeline
    rhi::RenderPipelineDesc pipelineDesc;
    pipelineDesc.label = "SkyboxPipeline";
    pipelineDesc.layout = m_pipelineLayout.get();

    // Shaders
    pipelineDesc.vertexShader = m_vertexShader.get();
    pipelineDesc.fragmentShader = m_fragmentShader.get();

    // No vertex input - fullscreen triangle generated in shader
    // (vertex.buffers is empty by default)

    // Primitive state
    pipelineDesc.primitive.topology = rhi::PrimitiveTopology::TriangleList;
    pipelineDesc.primitive.cullMode = rhi::CullMode::None;  // No culling for fullscreen
    pipelineDesc.primitive.frontFace = rhi::FrontFace::CounterClockwise;

    // Depth state - test but don't write (skybox is background)
    rhi::DepthStencilState depthStencilState;
    depthStencilState.depthTestEnabled = true;
    depthStencilState.depthWriteEnabled = false;  // Don't write depth
    depthStencilState.depthCompare = rhi::CompareOp::LessOrEqual;
    depthStencilState.format = depthFormat;
    pipelineDesc.depthStencil = &depthStencilState;

    // Color target - no blending
    rhi::ColorTargetState colorTarget;
    colorTarget.format = colorFormat;
    colorTarget.blend.blendEnabled = false;
    pipelineDesc.colorTargets.push_back(colorTarget);

    // Native render pass for Linux
    pipelineDesc.nativeRenderPass = nativeRenderPass;

    m_pipeline = m_device->createRenderPipeline(pipelineDesc);
    if (!m_pipeline) {
        std::cerr << "[SkyboxRenderer] Failed to create render pipeline\n";
        return false;
    }

    std::cout << "[SkyboxRenderer] Pipeline created\n";
    return true;
}

void SkyboxRenderer::update(const glm::mat4& invViewProj, const glm::vec3& sunDirection, float time) {
    m_sunDirection = glm::normalize(sunDirection);
}

void SkyboxRenderer::render(rhi::RHIRenderPassEncoder* renderPass, uint32_t frameIndex,
                            const glm::mat4& invViewProj, float time) {
    if (!m_pipeline || !renderPass) {
        return;
    }

    uint32_t bufferIndex = frameIndex % MAX_FRAMES_IN_FLIGHT;

    // Update uniform buffer
    UniformData uniformData{};
    uniformData.invViewProj = invViewProj;
    uniformData.sunDirection = m_sunDirection;
    uniformData.time = time;

    // Write to uniform buffer
    auto* buffer = m_uniformBuffers[bufferIndex].get();
    if (buffer) {
        void* mappedData = buffer->getMappedData();
        if (mappedData) {
            memcpy(mappedData, &uniformData, sizeof(UniformData));
        }
    }

    // Set pipeline and bind group
    renderPass->setPipeline(m_pipeline.get());
    renderPass->setBindGroup(0, m_bindGroups[bufferIndex].get());

    // Draw fullscreen triangle (3 vertices, no vertex buffer)
    renderPass->draw(3, 1, 0, 0);
}

} // namespace rendering
