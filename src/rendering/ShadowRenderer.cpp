#include "ShadowRenderer.hpp"
#include "src/utils/FileUtils.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>
#include <iostream>

#ifdef __linux__
#include <rhi/vulkan/VulkanRHIDevice.hpp>
#include <rhi/vulkan/VulkanRHITexture.hpp>
#endif

namespace rendering {

ShadowRenderer::ShadowRenderer(rhi::RHIDevice* device, rhi::RHIQueue* queue)
    : m_device(device), m_queue(queue) {
}

ShadowRenderer::~ShadowRenderer() {
#ifdef __linux__
    // Clean up native Vulkan resources
    if (m_device) {
        auto* vulkanDevice = dynamic_cast<RHI::Vulkan::VulkanRHIDevice*>(m_device);
        if (vulkanDevice) {
            VkDevice vkDevice = static_cast<VkDevice>(*vulkanDevice->getVkDevice());
            if (m_nativeFramebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(vkDevice, m_nativeFramebuffer, nullptr);
            }
            if (m_nativeRenderPass != VK_NULL_HANDLE) {
                vkDestroyRenderPass(vkDevice, m_nativeRenderPass, nullptr);
            }
        }
    }
#endif
}

bool ShadowRenderer::initialize(void* nativeRenderPass) {
    if (!m_device || !m_queue) {
        std::cerr << "[ShadowRenderer] Invalid device or queue\n";
        return false;
    }

    if (!createShadowMap()) {
        std::cerr << "[ShadowRenderer] Failed to create shadow map\n";
        return false;
    }

    if (!createShadowSampler()) {
        std::cerr << "[ShadowRenderer] Failed to create shadow sampler\n";
        return false;
    }

    if (!createShaders()) {
        std::cerr << "[ShadowRenderer] Failed to create shaders\n";
        return false;
    }

    if (!createUniformBuffers()) {
        std::cerr << "[ShadowRenderer] Failed to create uniform buffers\n";
        return false;
    }

    if (!createBindGroups()) {
        std::cerr << "[ShadowRenderer] Failed to create bind groups\n";
        return false;
    }

#ifdef __linux__
    // Linux: Create native Vulkan render pass and framebuffer for depth-only pass
    if (!createLinuxRenderPass()) {
        std::cerr << "[ShadowRenderer] Failed to create Linux render pass\n";
        return false;
    }

    if (!createLinuxFramebuffer()) {
        std::cerr << "[ShadowRenderer] Failed to create Linux framebuffer\n";
        return false;
    }

    // Use our native render pass for pipeline creation
    nativeRenderPass = m_nativeRenderPass;
#endif

    if (!createPipeline(nativeRenderPass)) {
        std::cerr << "[ShadowRenderer] Failed to create pipeline\n";
        return false;
    }

    m_initialized = true;
    std::cout << "[ShadowRenderer] Initialized successfully (" << SHADOW_MAP_SIZE << "x" << SHADOW_MAP_SIZE << ")\n";
    return true;
}

bool ShadowRenderer::createShadowMap() {
    // Create shadow map texture (depth-only)
    rhi::TextureDesc desc;
    desc.size = rhi::Extent3D(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 1);
    desc.format = rhi::TextureFormat::Depth32Float;
    desc.usage = rhi::TextureUsage::DepthStencil | rhi::TextureUsage::Sampled;
    desc.label = "ShadowMap";

    m_shadowMap = m_device->createTexture(desc);
    if (!m_shadowMap) {
        std::cerr << "[ShadowRenderer] Failed to create shadow map texture\n";
        return false;
    }

    // Create shadow map view
    rhi::TextureViewDesc viewDesc;
    viewDesc.format = rhi::TextureFormat::Depth32Float;
    viewDesc.dimension = rhi::TextureViewDimension::View2D;
    viewDesc.label = "ShadowMapView";

    m_shadowMapView = m_shadowMap->createView(viewDesc);
    if (!m_shadowMapView) {
        std::cerr << "[ShadowRenderer] Failed to create shadow map view\n";
        return false;
    }

    std::cout << "[ShadowRenderer] Shadow map created\n";
    return true;
}

bool ShadowRenderer::createShadowSampler() {
    rhi::SamplerDesc desc;
    // Use Nearest filter for depth textures (Linear not supported on D32_SFLOAT)
    desc.magFilter = rhi::FilterMode::Nearest;
    desc.minFilter = rhi::FilterMode::Nearest;
    desc.mipmapFilter = rhi::MipmapMode::Nearest;
    desc.addressModeU = rhi::AddressMode::ClampToEdge;
    desc.addressModeV = rhi::AddressMode::ClampToEdge;
    desc.addressModeW = rhi::AddressMode::ClampToEdge;
    // Note: compareEnable is for hardware shadow comparison (sampler2DShadow)
    // We use regular sampler2D and do manual comparison in shader
    desc.compareEnable = false;
    desc.label = "ShadowSampler";

    m_shadowSampler = m_device->createSampler(desc);
    if (!m_shadowSampler) {
        std::cerr << "[ShadowRenderer] Failed to create shadow sampler\n";
        return false;
    }

    std::cout << "[ShadowRenderer] Shadow sampler created\n";
    return true;
}

bool ShadowRenderer::createShaders() {
    // Load shadow vertex shader (pre-compiled SPIR-V)
    auto vertCodeRaw = FileUtils::readFile("shaders/shadow.vert.spv");
    if (vertCodeRaw.empty()) {
        std::cerr << "[ShadowRenderer] Failed to load shadow.vert.spv\n";
        return false;
    }
    std::vector<uint8_t> vertCode(vertCodeRaw.begin(), vertCodeRaw.end());

    rhi::ShaderSource vertSource(rhi::ShaderLanguage::SPIRV, vertCode, rhi::ShaderStage::Vertex, "main");
    rhi::ShaderDesc vertDesc(vertSource, "ShadowVertexShader");

    m_vertexShader = m_device->createShader(vertDesc);
    if (!m_vertexShader) {
        std::cerr << "[ShadowRenderer] Failed to create vertex shader\n";
        return false;
    }

    // Load shadow fragment shader (empty, for depth-only pass)
    auto fragCodeRaw = FileUtils::readFile("shaders/shadow.frag.spv");
    if (fragCodeRaw.empty()) {
        std::cerr << "[ShadowRenderer] Failed to load shadow.frag.spv\n";
        return false;
    }
    std::vector<uint8_t> fragCode(fragCodeRaw.begin(), fragCodeRaw.end());

    rhi::ShaderSource fragSource(rhi::ShaderLanguage::SPIRV, fragCode, rhi::ShaderStage::Fragment, "main");
    rhi::ShaderDesc fragDesc(fragSource, "ShadowFragmentShader");

    m_fragmentShader = m_device->createShader(fragDesc);
    if (!m_fragmentShader) {
        std::cerr << "[ShadowRenderer] Failed to create fragment shader\n";
        return false;
    }

    std::cout << "[ShadowRenderer] Shaders created\n";
    return true;
}

bool ShadowRenderer::createUniformBuffers() {
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        rhi::BufferDesc desc;
        desc.size = sizeof(LightSpaceUBO);
        desc.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::MapWrite;
        desc.mappedAtCreation = false;
        desc.label = "ShadowUniformBuffer";

        m_uniformBuffers[i] = m_device->createBuffer(desc);
        if (!m_uniformBuffers[i]) {
            std::cerr << "[ShadowRenderer] Failed to create uniform buffer " << i << "\n";
            return false;
        }
    }
    return true;
}

bool ShadowRenderer::createBindGroups() {
    // Create bind group layout
    rhi::BindGroupLayoutDesc layoutDesc;
    layoutDesc.entries.push_back(rhi::BindGroupLayoutEntry(0, rhi::ShaderStage::Vertex, rhi::BindingType::UniformBuffer));
    layoutDesc.label = "ShadowBindGroupLayout";

    m_bindGroupLayout = m_device->createBindGroupLayout(layoutDesc);
    if (!m_bindGroupLayout) {
        std::cerr << "[ShadowRenderer] Failed to create bind group layout\n";
        return false;
    }

    // Create bind groups for each frame
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        rhi::BindGroupDesc groupDesc;
        groupDesc.layout = m_bindGroupLayout.get();
        groupDesc.entries.push_back(rhi::BindGroupEntry::Buffer(0, m_uniformBuffers[i].get(), 0, sizeof(LightSpaceUBO)));
        groupDesc.label = "ShadowBindGroup";

        m_bindGroups[i] = m_device->createBindGroup(groupDesc);
        if (!m_bindGroups[i]) {
            std::cerr << "[ShadowRenderer] Failed to create bind group " << i << "\n";
            return false;
        }
    }

    return true;
}

bool ShadowRenderer::createPipeline(void* nativeRenderPass) {
    // Create pipeline layout
    rhi::PipelineLayoutDesc layoutDesc;
    layoutDesc.bindGroupLayouts.push_back(m_bindGroupLayout.get());
    layoutDesc.label = "ShadowPipelineLayout";

    m_pipelineLayout = m_device->createPipelineLayout(layoutDesc);
    if (!m_pipelineLayout) {
        std::cerr << "[ShadowRenderer] Failed to create pipeline layout\n";
        return false;
    }

    // Create render pipeline (depth-only)
    rhi::RenderPipelineDesc pipelineDesc;
    pipelineDesc.label = "ShadowPipeline";
    pipelineDesc.layout = m_pipelineLayout.get();

    // Vertex and fragment shaders (fragment is empty but required by RHI)
    pipelineDesc.vertexShader = m_vertexShader.get();
    pipelineDesc.fragmentShader = m_fragmentShader.get();  // Empty shader for depth-only

    // Vertex input layout (must match building shader for instancing)
    // Per-vertex attributes (binding 0)
    rhi::VertexBufferLayout vertexLayout;
    vertexLayout.stride = sizeof(float) * 8;  // pos(3) + normal(3) + texCoord(2)
    vertexLayout.inputRate = rhi::VertexInputRate::Vertex;
    vertexLayout.attributes = {
        rhi::VertexAttribute(0, 0, rhi::TextureFormat::RGB32Float, 0),                 // position
        rhi::VertexAttribute(1, 0, rhi::TextureFormat::RGB32Float, sizeof(float) * 3), // normal
        rhi::VertexAttribute(2, 0, rhi::TextureFormat::RG32Float, sizeof(float) * 6)   // texCoord
    };
    pipelineDesc.vertex.buffers.push_back(vertexLayout);

    // Per-instance attributes (binding 1)
    rhi::VertexBufferLayout instanceLayout;
    instanceLayout.stride = 40;  // 3*4 + 3*4 + 3*4 + 4 padding = 40 bytes
    instanceLayout.inputRate = rhi::VertexInputRate::Instance;
    instanceLayout.attributes = {
        rhi::VertexAttribute(3, 1, rhi::TextureFormat::RGB32Float, 0),   // instancePosition
        rhi::VertexAttribute(4, 1, rhi::TextureFormat::RGB32Float, 12),  // instanceColor
        rhi::VertexAttribute(5, 1, rhi::TextureFormat::RGB32Float, 24)   // instanceScale
    };
    pipelineDesc.vertex.buffers.push_back(instanceLayout);

    // Primitive state
    pipelineDesc.primitive.topology = rhi::PrimitiveTopology::TriangleList;
    pipelineDesc.primitive.cullMode = rhi::CullMode::Back;  // Cull back faces
    pipelineDesc.primitive.frontFace = rhi::FrontFace::Clockwise;  // Match building pipeline

    // Depth state - write depth
    rhi::DepthStencilState depthStencilState;
    depthStencilState.depthTestEnabled = true;
    depthStencilState.depthWriteEnabled = true;
    depthStencilState.depthCompare = rhi::CompareOp::Less;
    depthStencilState.format = rhi::TextureFormat::Depth32Float;
    pipelineDesc.depthStencil = &depthStencilState;

    // No color targets (depth-only pass)
    // pipelineDesc.colorTargets is empty

    // Native render pass for Linux (if provided)
    pipelineDesc.nativeRenderPass = nativeRenderPass;

    m_pipeline = m_device->createRenderPipeline(pipelineDesc);
    if (!m_pipeline) {
        std::cerr << "[ShadowRenderer] Failed to create render pipeline\n";
        return false;
    }

    std::cout << "[ShadowRenderer] Pipeline created\n";
    return true;
}

void ShadowRenderer::updateLightMatrix(const glm::vec3& lightDir,
                                        const glm::vec3& sceneCenter,
                                        float sceneRadius) {
    // Orthographic projection for directional light
    // lightDir points TO the sun, so light comes FROM that direction
    // Place light position along the sun direction from scene center
    glm::vec3 normalizedLightDir = glm::normalize(lightDir);
    glm::vec3 lightPos = sceneCenter + normalizedLightDir * sceneRadius * 2.0f;

    // Look from light position towards scene center
    glm::mat4 lightView = glm::lookAt(lightPos, sceneCenter, glm::vec3(0.0f, 1.0f, 0.0f));

    // Orthographic projection to cover the scene
    // Buildings: -45 to +45 (90 units), Ground: -50 to +50 (100 units)
    // Use 55 to cover -55 to +55 with margin for full ground coverage
    float orthoSize = 55.0f;
    glm::mat4 lightProj = glm::ortho(-orthoSize, orthoSize,
                                     -orthoSize, orthoSize,
                                     0.1f, sceneRadius * 4.0f);

    m_lightSpaceMatrix = lightProj * lightView;
}

rhi::RHIRenderPassEncoder* ShadowRenderer::beginShadowPass(rhi::RHICommandEncoder* encoder, uint32_t frameIndex) {
    if (!m_initialized || !encoder) {
        return nullptr;
    }

    uint32_t bufferIndex = frameIndex % MAX_FRAMES_IN_FLIGHT;

    // Update uniform buffer with light space matrix
    LightSpaceUBO ubo;
    ubo.lightSpaceMatrix = m_lightSpaceMatrix;

    auto* buffer = m_uniformBuffers[bufferIndex].get();
    if (buffer) {
        void* mappedData = buffer->getMappedData();
        if (mappedData) {
            memcpy(mappedData, &ubo, sizeof(LightSpaceUBO));
        }
    }

    // Create render pass descriptor for shadow pass
    rhi::RenderPassDesc passDesc;
    passDesc.width = SHADOW_MAP_SIZE;
    passDesc.height = SHADOW_MAP_SIZE;
    passDesc.label = "ShadowPass";

    // Depth attachment only (no color)
    rhi::RenderPassDepthStencilAttachment depthAttachment;
    depthAttachment.view = m_shadowMapView.get();
    depthAttachment.depthLoadOp = rhi::LoadOp::Clear;
    depthAttachment.depthStoreOp = rhi::StoreOp::Store;
    depthAttachment.depthClearValue = 1.0f;
    depthAttachment.depthReadOnly = false;
    passDesc.depthStencilAttachment = &depthAttachment;

    // No color attachments
    // passDesc.colorAttachments is empty

#ifdef __linux__
    // Linux: Use native Vulkan render pass and framebuffer
    passDesc.nativeRenderPass = m_nativeRenderPass;
    passDesc.nativeFramebuffer = m_nativeFramebuffer;
#endif

    m_currentRenderPass = encoder->beginRenderPass(passDesc);
    if (!m_currentRenderPass) {
        std::cerr << "[ShadowRenderer] Failed to begin shadow pass\n";
        return nullptr;
    }

    // Set viewport and scissor
    m_currentRenderPass->setViewport(0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 0.0f, 1.0f);
    m_currentRenderPass->setScissorRect(0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);

    // Set pipeline and bind group
    m_currentRenderPass->setPipeline(m_pipeline.get());
    m_currentRenderPass->setBindGroup(0, m_bindGroups[bufferIndex].get());

    return m_currentRenderPass.get();
}

void ShadowRenderer::endShadowPass() {
    if (m_currentRenderPass) {
        m_currentRenderPass->end();
        m_currentRenderPass.reset();
    }
}

#ifdef __linux__
bool ShadowRenderer::createLinuxRenderPass() {
    auto* vulkanDevice = dynamic_cast<RHI::Vulkan::VulkanRHIDevice*>(m_device);
    if (!vulkanDevice) {
        std::cerr << "[ShadowRenderer] Failed to get Vulkan device\n";
        return false;
    }

    VkDevice vkDevice = static_cast<VkDevice>(*vulkanDevice->getVkDevice());

    // Depth-only attachment
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;  // Need to sample later
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;  // Ready for sampling

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 0;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pColorAttachments = nullptr;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    // Single dependency for layout transition
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &depthAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VkResult result = vkCreateRenderPass(vkDevice, &renderPassInfo, nullptr, &m_nativeRenderPass);
    if (result != VK_SUCCESS) {
        std::cerr << "[ShadowRenderer] Failed to create Vulkan render pass: " << result << "\n";
        return false;
    }

    std::cout << "[ShadowRenderer] Linux render pass created\n";
    return true;
}

bool ShadowRenderer::createLinuxFramebuffer() {
    auto* vulkanDevice = dynamic_cast<RHI::Vulkan::VulkanRHIDevice*>(m_device);
    if (!vulkanDevice) {
        return false;
    }

    VkDevice vkDevice = static_cast<VkDevice>(*vulkanDevice->getVkDevice());

    // Get the shadow map image view
    auto* vulkanView = dynamic_cast<RHI::Vulkan::VulkanRHITextureView*>(m_shadowMapView.get());
    if (!vulkanView) {
        std::cerr << "[ShadowRenderer] Failed to get Vulkan texture view\n";
        return false;
    }

    VkImageView depthView = static_cast<VkImageView>(vulkanView->getVkImageView());

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_nativeRenderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &depthView;
    framebufferInfo.width = SHADOW_MAP_SIZE;
    framebufferInfo.height = SHADOW_MAP_SIZE;
    framebufferInfo.layers = 1;

    VkResult result = vkCreateFramebuffer(vkDevice, &framebufferInfo, nullptr, &m_nativeFramebuffer);
    if (result != VK_SUCCESS) {
        std::cerr << "[ShadowRenderer] Failed to create Vulkan framebuffer: " << result << "\n";
        return false;
    }

    std::cout << "[ShadowRenderer] Linux framebuffer created\n";
    return true;
}
#endif

} // namespace rendering
