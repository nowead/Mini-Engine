#include "Renderer.hpp"
#include "src/ui/ImGuiManager.hpp"
#include "src/rhi/vulkan/VulkanRHICommandEncoder.hpp"

#include <stdexcept>
#include <iostream>

// Phase 7: LegacyCommandBufferAdapter removed - ImGui now uses RHI directly

Renderer::Renderer(GLFWwindow* window,
                   const std::vector<const char*>& validationLayers,
                   bool enableValidation,
                   bool useFdfMode)
    : window(window),
      startTime(std::chrono::high_resolution_clock::now()),
      viewMatrix(glm::mat4(1.0f)),
      projectionMatrix(glm::mat4(1.0f)),
      fdfMode(useFdfMode) {

    // Create core device
    device = std::make_unique<VulkanDevice>(validationLayers, enableValidation);
    device->createSurface(window);
    device->createLogicalDevice();

    // Phase 4: Initialize RHI Bridge (parallel to legacy device)
    // Note: RHI Bridge creates its own VulkanRHIDevice internally
    // In future phases, we'll use this as the primary device
    rhiBridge = std::make_unique<rendering::RendererBridge>(window, enableValidation);

    // Create rendering components
    swapchain = std::make_unique<VulkanSwapchain>(*device, window);
    createDepthResources();

    // Select shader and topology based on mode
    std::string shaderPath = fdfMode ? "shaders/fdf.spv" : "shaders/slang.spv";
    TopologyMode topology = fdfMode ? TopologyMode::LineList : TopologyMode::TriangleList;

    // Platform-specific pipeline creation
#ifdef __linux__
    // Linux: Create render pass and framebuffers for traditional rendering
    swapchain->createRenderPass(findDepthFormat());
    std::vector<vk::ImageView> depthViews(swapchain->getImageCount(), depthImage->getImageView());
    swapchain->createFramebuffers(depthViews);

    // Create pipeline with render pass
    pipeline = std::make_unique<VulkanPipeline>(
        *device, *swapchain, shaderPath, findDepthFormat(), swapchain->getRenderPass(), topology);
#else
    // macOS/Windows: Create pipeline with dynamic rendering
    pipeline = std::make_unique<VulkanPipeline>(
        *device, *swapchain, shaderPath, findDepthFormat(), nullptr, topology);
#endif

    // Phase 7: CommandManager removed - now using RHI command encoding
    // Create sync manager (still needed for legacy swapchain synchronization)
    syncManager = std::make_unique<SyncManager>(
        *device, MAX_FRAMES_IN_FLIGHT, swapchain->getImageCount());

    // Create high-level managers using RHI (Phase 5)
    auto* rhiDevice = rhiBridge->getDevice();
    auto* rhiQueue = rhiDevice->getQueue(rhi::QueueType::Graphics);
    resourceManager = std::make_unique<ResourceManager>(rhiDevice, rhiQueue);
    sceneManager = std::make_unique<SceneManager>(rhiDevice, rhiQueue);

    // Create uniform buffers and descriptors
    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();

    // Phase 4: Create RHI resources (parallel to legacy for testing)
    createRHIDepthResources();
    createRHIUniformBuffers();
    createRHIBindGroups();
    createRHIPipeline();  // Phase 4.4

    // Initialize descriptors (FDF mode doesn't need texture)
    if (fdfMode) {
        updateDescriptorSets();
    }
}

Renderer::~Renderer() {
    // RAII: Wait for device idle before destroying Vulkan resources
    if (device) {
        device->getDevice().waitIdle();
    }
    // All other resources cleaned up by RAII in reverse declaration order
}

void Renderer::loadModel(const std::string& modelPath) {
    currentModelPath = modelPath;
    sceneManager->loadMesh(modelPath, zScale);  // Delegates to SceneManager

    // Phase 4.5: Create RHI buffers after loading mesh
    createRHIBuffers();
}

void Renderer::loadTexture(const std::string& texturePath) {
    resourceManager->loadTexture(texturePath);  // Delegates to ResourceManager
    updateDescriptorSets();  // Update descriptors with new texture
}

// Phase 7: Legacy rendering methods (drawFrameLegacy, recordCommandBuffer, updateUniformBuffer, transitionImageLayout) removed
// Now using RHI-based rendering via drawFrame()

void Renderer::waitIdle() {
    device->getDevice().waitIdle();
}

void Renderer::handleFramebufferResize() {
    recreateSwapchain();
}

void Renderer::updateCamera(const glm::mat4& view, const glm::mat4& projection) {
    viewMatrix = view;
    projectionMatrix = projection;
}

void Renderer::adjustZScale(float delta) {
    if (!fdfMode || currentModelPath.empty()) {
        return;  // Only works in FDF mode with a loaded model
    }

    zScale += delta;
    zScale = std::max(0.1f, std::min(zScale, 50.0f));  // Clamp between 0.1 and 10.0

    // Wait for device idle before reloading
    device->getDevice().waitIdle();

    // Clear existing meshes and reload with new Z-scale (using RHI)
    auto* rhiDevice = rhiBridge->getDevice();
    auto* rhiQueue = rhiDevice->getQueue(rhi::QueueType::Graphics);
    sceneManager = std::make_unique<SceneManager>(rhiDevice, rhiQueue);
    sceneManager->loadMesh(currentModelPath, zScale);
}

glm::vec3 Renderer::getMeshCenter() const {
    auto* mesh = sceneManager->getPrimaryMesh();
    if (mesh) {
        return mesh->getBoundingBoxCenter();
    }
    return glm::vec3(0.0f, 0.0f, 0.0f);
}

float Renderer::getMeshRadius() const {
    auto* mesh = sceneManager->getPrimaryMesh();
    if (mesh) {
        return mesh->getBoundingBoxRadius();
    }
    return 0.0f;
}

void Renderer::createDepthResources() {
    vk::Format depthFormat = findDepthFormat();

    depthImage = std::make_unique<VulkanImage>(*device,
        swapchain->getExtent().width, swapchain->getExtent().height,
        depthFormat,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eDepthStencilAttachment,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        vk::ImageAspectFlagBits::eDepth);
}

void Renderer::createUniformBuffers() {
    uniformBuffers.clear();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vk::DeviceSize bufferSize = sizeof(UniformBufferObject);
        auto uniformBuffer = std::make_unique<VulkanBuffer>(*device, bufferSize,
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

        // Map the buffer for persistent mapping
        uniformBuffer->map();
        uniformBuffers.emplace_back(std::move(uniformBuffer));
    }
}

void Renderer::createDescriptorPool() {
    std::array poolSizes {
        vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, MAX_FRAMES_IN_FLIGHT),
        vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, MAX_FRAMES_IN_FLIGHT)
    };
    vk::DescriptorPoolCreateInfo poolInfo{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = MAX_FRAMES_IN_FLIGHT,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data()
    };
    descriptorPool = vk::raii::DescriptorPool(device->getDevice(), poolInfo);
}

void Renderer::createDescriptorSets() {
    std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, pipeline->getDescriptorSetLayout());
    vk::DescriptorSetAllocateInfo allocInfo{
        .descriptorPool = descriptorPool,
        .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
        .pSetLayouts = layouts.data()
    };

    descriptorSets.clear();
    descriptorSets = device->getDevice().allocateDescriptorSets(allocInfo);
}

void Renderer::updateDescriptorSets() {
    if (fdfMode) {
        // FDF mode: Only update uniform buffer (no texture)
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vk::DescriptorBufferInfo bufferInfo{
                .buffer = uniformBuffers[i]->getHandle(),
                .offset = 0,
                .range = sizeof(UniformBufferObject)
            };

            vk::WriteDescriptorSet descriptorWrite{
                .dstSet = descriptorSets[i],
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .pBufferInfo = &bufferInfo
            };

            device->getDevice().updateDescriptorSets(descriptorWrite, {});
        }
    } else {
        // OBJ mode: Update both uniform buffer and texture
        // TODO Phase 5: Legacy descriptor updates - needs RHI texture view integration
        // Temporarily disabled until descriptor set migration complete
        /*
        auto* textureImage = resourceManager->getTexture("textures/viking_room.png");
        if (!textureImage) {
            return;  // Texture not loaded yet
        }
        */
        return;  // Skip legacy descriptor updates for now

        /*
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vk::DescriptorBufferInfo bufferInfo{
                .buffer = uniformBuffers[i]->getHandle(),
                .offset = 0,
                .range = sizeof(UniformBufferObject)
            };
            vk::DescriptorImageInfo imageInfo{
                .sampler = textureImage->getSampler(),
                .imageView = textureImage->getImageView(),
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
            };
            std::array descriptorWrites{
                vk::WriteDescriptorSet{
                    .dstSet = descriptorSets[i],
                    .dstBinding = 0,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eUniformBuffer,
                    .pBufferInfo = &bufferInfo
                },
                vk::WriteDescriptorSet{
                    .dstSet = descriptorSets[i],
                    .dstBinding = 1,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                    .pImageInfo = &imageInfo
                }
            };
            device->getDevice().updateDescriptorSets(descriptorWrites, {});
        }
        */
    }
}

// Phase 7: Legacy rendering methods removed (recordCommandBuffer, updateUniformBuffer, transitionImageLayout)
// Now using RHI-based rendering via drawFrame()

void Renderer::recreateSwapchain() {
    // Wait for window to be visible
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }

    device->getDevice().waitIdle();

    swapchain->recreate();
    createDepthResources();

    // Phase 6: Notify ImGui of resize
    if (imguiManager) {
        imguiManager->handleResize();
    }
}

vk::Format Renderer::findDepthFormat() {
    return device->findSupportedFormat(
        {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint},
        vk::ImageTiling::eOptimal,
        vk::FormatFeatureFlagBits::eDepthStencilAttachment
    );
}

void Renderer::initImGui(GLFWwindow* window) {
    // Phase 6: Create ImGui manager using RHI types
    auto* rhiDevice = rhiBridge->getDevice();
    auto* rhiSwapchain = rhiBridge->getSwapchain();

    if (rhiDevice && rhiSwapchain) {
        imguiManager = std::make_unique<ImGuiManager>(window, rhiDevice, rhiSwapchain);
    }
}

// ============================================================================
// Phase 4: RHI Resource Creation (parallel to legacy resources)
// ============================================================================

void Renderer::createRHIDepthResources() {
    if (!rhiBridge || !rhiBridge->isReady()) {
        return;
    }

    auto* rhiDevice = rhiBridge->getDevice();

    // Create depth texture using RHI
    rhi::TextureDesc depthDesc;
    depthDesc.size = rhi::Extent3D(swapchain->getExtent().width, swapchain->getExtent().height, 1);
    depthDesc.format = rhi::TextureFormat::Depth32Float;  // Corresponds to vk::Format::eD32Sfloat
    depthDesc.usage = rhi::TextureUsage::DepthStencil;
    depthDesc.label = "RHI Depth Image";

    rhiDepthImage = rhiDevice->createTexture(depthDesc);

    // Create cached depth image view
    if (rhiDepthImage) {
        rhi::TextureViewDesc viewDesc;
        viewDesc.format = rhi::TextureFormat::Depth32Float;
        viewDesc.dimension = rhi::TextureViewDimension::View2D;
        rhiDepthImageView = rhiDepthImage->createView(viewDesc);
    }
}

void Renderer::createRHIUniformBuffers() {
    if (!rhiBridge || !rhiBridge->isReady()) {
        return;
    }

    auto* rhiDevice = rhiBridge->getDevice();
    rhiUniformBuffers.clear();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        rhi::BufferDesc bufferDesc;
        bufferDesc.size = sizeof(UniformBufferObject);
        bufferDesc.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::MapWrite;
        bufferDesc.mappedAtCreation = true;
        bufferDesc.label = "RHI Uniform Buffer";

        rhiUniformBuffers.push_back(rhiDevice->createBuffer(bufferDesc));
    }
}

void Renderer::createRHIBindGroups() {
    if (!rhiBridge || !rhiBridge->isReady() || rhiUniformBuffers.empty()) {
        return;
    }

    auto* rhiDevice = rhiBridge->getDevice();

    // Create bind group layout
    rhi::BindGroupLayoutDesc layoutDesc;

    // Binding 0: Uniform buffer
    rhi::BindGroupLayoutEntry uboEntry;
    uboEntry.binding = 0;
    uboEntry.visibility = rhi::ShaderStage::Vertex;
    uboEntry.type = rhi::BindingType::UniformBuffer;
    layoutDesc.entries.push_back(uboEntry);

    // Binding 1: Combined image sampler (only for OBJ mode)
    if (!fdfMode) {
        rhi::BindGroupLayoutEntry samplerEntry;
        samplerEntry.binding = 1;
        samplerEntry.visibility = rhi::ShaderStage::Fragment;
        samplerEntry.type = rhi::BindingType::SampledTexture;
        layoutDesc.entries.push_back(samplerEntry);
    }

    layoutDesc.label = "RHI Main Bind Group Layout";
    rhiBindGroupLayout = rhiDevice->createBindGroupLayout(layoutDesc);

    // Create bind groups for each frame
    rhiBindGroups.clear();
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        rhi::BindGroupDesc bindGroupDesc;
        bindGroupDesc.layout = rhiBindGroupLayout.get();

        // Add uniform buffer entry
        bindGroupDesc.entries.push_back(
            rhi::BindGroupEntry::Buffer(0, rhiUniformBuffers[i].get())
        );

        bindGroupDesc.label = "RHI Main Bind Group";
        rhiBindGroups.push_back(rhiDevice->createBindGroup(bindGroupDesc));
    }
}

// ============================================================================
// Phase 4.4: RHI Pipeline Creation
// ============================================================================

void Renderer::createRHIPipeline() {
    if (!rhiBridge || !rhiBridge->isReady() || !rhiBindGroupLayout) {
        return;
    }

    // Select shader path based on mode
    std::string shaderPath = fdfMode ? "shaders/fdf.spv" : "shaders/slang.spv";

    // Create vertex shader
    rhiVertexShader = rhiBridge->createShaderFromFile(
        shaderPath,
        rhi::ShaderStage::Vertex,
        "vertMain"
    );

    // Create fragment shader
    rhiFragmentShader = rhiBridge->createShaderFromFile(
        shaderPath,
        rhi::ShaderStage::Fragment,
        "fragMain"
    );

    if (!rhiVertexShader || !rhiFragmentShader) {
        std::cerr << "[Renderer] Failed to create RHI shaders\n";
        return;
    }

    // Create pipeline layout
    rhi::PipelineLayoutDesc layoutDesc;
    layoutDesc.bindGroupLayouts.push_back(rhiBindGroupLayout.get());
    rhiPipelineLayout = rhiBridge->createPipelineLayout(layoutDesc);

    if (!rhiPipelineLayout) {
        std::cerr << "[Renderer] Failed to create RHI pipeline layout\n";
        return;
    }

    // Setup vertex state - matches Vertex struct
    rhi::VertexBufferLayout vertexLayout;
    vertexLayout.stride = sizeof(Vertex);
    vertexLayout.inputRate = rhi::VertexInputRate::Vertex;
    vertexLayout.attributes = {
        rhi::VertexAttribute(0, 0, rhi::TextureFormat::RGB32Float, offsetof(Vertex, pos)),    // position
        rhi::VertexAttribute(1, 0, rhi::TextureFormat::RGB32Float, offsetof(Vertex, color)),  // color
        rhi::VertexAttribute(2, 0, rhi::TextureFormat::RG32Float, offsetof(Vertex, texCoord)) // texCoord
    };

    // Create render pipeline descriptor
    rhi::RenderPipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = rhiVertexShader.get();
    pipelineDesc.fragmentShader = rhiFragmentShader.get();
    pipelineDesc.layout = rhiPipelineLayout.get();
    pipelineDesc.vertex.buffers.push_back(vertexLayout);

    // Primitive state - wireframe for FDF mode
    pipelineDesc.primitive.topology = fdfMode
        ? rhi::PrimitiveTopology::LineList
        : rhi::PrimitiveTopology::TriangleList;
    pipelineDesc.primitive.cullMode = fdfMode
        ? rhi::CullMode::None
        : rhi::CullMode::Back;
    pipelineDesc.primitive.frontFace = rhi::FrontFace::CounterClockwise;

    // Depth-stencil state
    rhi::DepthStencilState depthStencilState;
    depthStencilState.depthTestEnabled = true;
    depthStencilState.depthWriteEnabled = !fdfMode;  // Disable depth write for wireframe
    depthStencilState.depthCompare = rhi::CompareOp::Less;
    depthStencilState.format = rhi::TextureFormat::Depth32Float;
    pipelineDesc.depthStencil = &depthStencilState;

    // Color target - swapchain format
    rhi::ColorTargetState colorTarget;
    colorTarget.format = rhi::TextureFormat::BGRA8Unorm;  // Common swapchain format
    colorTarget.blend.blendEnabled = false;
    pipelineDesc.colorTargets.push_back(colorTarget);

    pipelineDesc.label = "RHI Main Pipeline";

    // Create pipeline
    rhiPipeline = rhiBridge->createRenderPipeline(pipelineDesc);

    if (rhiPipeline) {
        std::cout << "[Renderer] RHI Pipeline created successfully\n";
    } else {
        std::cerr << "[Renderer] Failed to create RHI pipeline\n";
    }
}

// ============================================================================
// Phase 4.5: RHI Vertex/Index Buffer Creation
// ============================================================================

void Renderer::createRHIBuffers() {
    if (!rhiBridge || !rhiBridge->isReady() || !sceneManager) {
        return;
    }

    auto* mesh = sceneManager->getPrimaryMesh();
    if (!mesh || !mesh->hasData()) {
        return;
    }

    auto* rhiDevice = rhiBridge->getDevice();

    // Get raw vertex and index data from mesh
    const auto& vertices = mesh->getVertices();
    const auto& indices = mesh->getIndices();

    size_t vertexCount = vertices.size();
    size_t indexCount = indices.size();
    size_t vertexBufferSize = vertexCount * sizeof(Vertex);
    size_t indexBufferSize = indexCount * sizeof(uint32_t);

    // Create vertex staging buffer (host-visible, mapped at creation)
    rhi::BufferDesc vertexStagingDesc;
    vertexStagingDesc.size = vertexBufferSize;
    vertexStagingDesc.usage = rhi::BufferUsage::CopySrc | rhi::BufferUsage::MapWrite;
    vertexStagingDesc.mappedAtCreation = true;
    vertexStagingDesc.label = "RHI Vertex Staging Buffer";

    auto vertexStagingBuffer = rhiDevice->createBuffer(vertexStagingDesc);

    // Copy vertex data to staging buffer
    if (vertexStagingBuffer) {
        void* mappedData = vertexStagingBuffer->getMappedData();
        if (mappedData) {
            memcpy(mappedData, vertices.data(), vertexBufferSize);
            vertexStagingBuffer->unmap();
        }
    }

    // Create device-local vertex buffer
    rhi::BufferDesc vertexBufferDesc;
    vertexBufferDesc.size = vertexBufferSize;
    vertexBufferDesc.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::CopyDst;
    vertexBufferDesc.mappedAtCreation = false;
    vertexBufferDesc.label = "RHI Vertex Buffer";

    rhiVertexBuffer = rhiDevice->createBuffer(vertexBufferDesc);

    // Create index staging buffer
    rhi::BufferDesc indexStagingDesc;
    indexStagingDesc.size = indexBufferSize;
    indexStagingDesc.usage = rhi::BufferUsage::CopySrc | rhi::BufferUsage::MapWrite;
    indexStagingDesc.mappedAtCreation = true;
    indexStagingDesc.label = "RHI Index Staging Buffer";

    auto indexStagingBuffer = rhiDevice->createBuffer(indexStagingDesc);

    // Copy index data to staging buffer
    if (indexStagingBuffer) {
        void* mappedData = indexStagingBuffer->getMappedData();
        if (mappedData) {
            memcpy(mappedData, indices.data(), indexBufferSize);
            indexStagingBuffer->unmap();
        }
    }

    // Create device-local index buffer
    rhi::BufferDesc indexBufferDesc;
    indexBufferDesc.size = indexBufferSize;
    indexBufferDesc.usage = rhi::BufferUsage::Index | rhi::BufferUsage::CopyDst;
    indexBufferDesc.mappedAtCreation = false;
    indexBufferDesc.label = "RHI Index Buffer";

    rhiIndexBuffer = rhiDevice->createBuffer(indexBufferDesc);
    rhiIndexCount = static_cast<uint32_t>(indexCount);

    // Copy data from staging to device-local buffers using command buffer
    if (rhiVertexBuffer && rhiIndexBuffer && vertexStagingBuffer && indexStagingBuffer) {
        auto encoder = rhiDevice->createCommandEncoder();
        if (encoder) {
            encoder->copyBufferToBuffer(
                vertexStagingBuffer.get(), 0,
                rhiVertexBuffer.get(), 0,
                vertexBufferSize
            );
            encoder->copyBufferToBuffer(
                indexStagingBuffer.get(), 0,
                rhiIndexBuffer.get(), 0,
                indexBufferSize
            );

            auto commandBuffer = encoder->finish();
            if (commandBuffer) {
                // Submit and wait for completion
                auto* queue = rhiDevice->getQueue(rhi::QueueType::Graphics);
                auto fence = rhiDevice->createFence(false);
                queue->submit(commandBuffer.get(), fence.get());
                fence->wait();
            }
        }

        std::cout << "[Renderer] RHI buffers uploaded: " 
                  << vertexCount << " vertices (" << vertexBufferSize << " bytes), " 
                  << indexCount << " indices (" << indexBufferSize << " bytes)\n";
    }
}

// ============================================================================
// Phase 4.2: RHI Command Recording (parallel to legacy recording)
// ============================================================================

void Renderer::recordRHICommandBuffer(uint32_t imageIndex) {
    // This function demonstrates RHI command recording alongside legacy Vulkan
    // In the final migration phase, this will replace recordCommandBuffer()

    if (!rhiBridge || !rhiBridge->isReady()) {
        return;
    }

    // Create command encoder for this frame
    auto encoder = rhiBridge->createCommandEncoder();
    if (!encoder) {
        return;
    }

    // Get current swapchain view for rendering
    auto* swapchainView = rhiBridge->getCurrentSwapchainView();
    if (!swapchainView) {
        return;
    }

    // Setup render pass descriptor
    rhi::RenderPassDesc renderPassDesc;
    renderPassDesc.width = swapchain->getExtent().width;
    renderPassDesc.height = swapchain->getExtent().height;

    // Color attachment (swapchain image)
    rhi::RenderPassColorAttachment colorAttachment;
    colorAttachment.view = swapchainView;
    colorAttachment.loadOp = rhi::LoadOp::Clear;
    colorAttachment.storeOp = rhi::StoreOp::Store;
    colorAttachment.clearValue = rhi::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
    renderPassDesc.colorAttachments.push_back(colorAttachment);

    // Depth attachment
    rhi::RenderPassDepthStencilAttachment depthAttachmentStorage;
    if (rhiDepthImage) {
        // Create view for depth image
        rhi::TextureViewDesc depthViewDesc;
        depthViewDesc.format = rhi::TextureFormat::Depth32Float;
        depthViewDesc.dimension = rhi::TextureViewDimension::View2D;
        auto depthView = rhiDepthImage->createView(depthViewDesc);

        depthAttachmentStorage.view = depthView.get();  // Note: view ownership issue - would need caching
        depthAttachmentStorage.depthLoadOp = rhi::LoadOp::Clear;
        depthAttachmentStorage.depthStoreOp = rhi::StoreOp::DontCare;
        depthAttachmentStorage.depthClearValue = 1.0f;
        depthAttachmentStorage.stencilLoadOp = rhi::LoadOp::DontCare;
        depthAttachmentStorage.stencilStoreOp = rhi::StoreOp::DontCare;
        // Note: For proper implementation, depth view should be cached
        // renderPassDesc.depthStencilAttachment = &depthAttachmentStorage;
    }

    // Begin render pass
    auto renderPassEncoder = encoder->beginRenderPass(renderPassDesc);
    if (!renderPassEncoder) {
        return;
    }

    // Set viewport and scissor
    renderPassEncoder->setViewport(
        0.0f, 0.0f,
        static_cast<float>(swapchain->getExtent().width),
        static_cast<float>(swapchain->getExtent().height),
        0.0f, 1.0f
    );
    renderPassEncoder->setScissorRect(
        0, 0,
        swapchain->getExtent().width,
        swapchain->getExtent().height
    );

    // TODO: In future phases:
    // - Set RHI pipeline (when RHI pipeline is created)
    // - Set bind groups
    // - Bind vertex/index buffers from SceneManager
    // - Draw calls

    // End render pass
    renderPassEncoder->end();

    // Finish encoding and store command buffer
    // Note: In production, we would submit this to the queue
    auto commandBuffer = encoder->finish();

    // For now, we just verify the command buffer was created successfully
    // In future phases, this will be submitted via rhiBridge->submitCommandBuffer()
    (void)commandBuffer;  // Suppress unused variable warning
}

void Renderer::updateRHIUniformBuffer(uint32_t currentImage) {
    if (currentImage >= rhiUniformBuffers.size() || !rhiUniformBuffers[currentImage]) {
        return;
    }

    UniformBufferObject ubo{};
    ubo.model = glm::mat4(1.0f);
    ubo.view = viewMatrix;
    ubo.proj = projectionMatrix;

    // Copy to RHI uniform buffer (if mapped)
    auto* buffer = rhiUniformBuffers[currentImage].get();
    if (buffer) {
        void* mappedData = buffer->getMappedData();
        if (mappedData) {
            memcpy(mappedData, &ubo, sizeof(ubo));
        }
    }
}

// ============================================================================
// Phase 7: Primary RHI Render Loop (migrated from drawFrameRHI)
// ============================================================================

void Renderer::drawFrame() {
    // Complete RHI rendering path using RHI abstractions
    // Phase 7: Replaces legacy Vulkan rendering (now drawFrameLegacy)

    if (!rhiBridge || !rhiBridge->isReady()) {
        return;
    }

    // Initialize swapchain if not already done
    if (!rhiBridge->getSwapchain()) {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        rhiBridge->createSwapchain(static_cast<uint32_t>(width), static_cast<uint32_t>(height), true);
    }

    // Step 1: Begin frame (wait for fence, acquire swapchain image)
    if (!rhiBridge->beginFrame()) {
        // Swapchain needs recreation - skip this frame
        return;
    }

    uint32_t frameIndex = rhiBridge->getCurrentFrameIndex();

    // Step 2: Update uniform buffer with RHI
    updateRHIUniformBuffer(frameIndex);

    // Step 3: Create and record command buffer
    auto encoder = rhiBridge->createCommandEncoder();
    if (!encoder) {
        return;
    }

    // Get swapchain view
    auto* swapchainView = rhiBridge->getCurrentSwapchainView();
    if (!swapchainView) {
        return;
    }

    // Setup render pass
    rhi::RenderPassDesc renderPassDesc;
    renderPassDesc.width = rhiBridge->getSwapchain()->getWidth();
    renderPassDesc.height = rhiBridge->getSwapchain()->getHeight();
    renderPassDesc.label = "RHI Main Render Pass";

    // Color attachment
    rhi::RenderPassColorAttachment colorAttachment;
    colorAttachment.view = swapchainView;
    colorAttachment.loadOp = rhi::LoadOp::Clear;
    colorAttachment.storeOp = rhi::StoreOp::Store;
    colorAttachment.clearValue = rhi::ClearColorValue(0.1f, 0.1f, 0.2f, 1.0f);  // Dark blue
    renderPassDesc.colorAttachments.push_back(colorAttachment);

    // Depth attachment (if available)
    rhi::RenderPassDepthStencilAttachment depthAttachment;
    if (rhiDepthImageView) {
        depthAttachment.view = rhiDepthImageView.get();
        depthAttachment.depthLoadOp = rhi::LoadOp::Clear;
        depthAttachment.depthStoreOp = rhi::StoreOp::Store;
        depthAttachment.depthClearValue = 1.0f;
        renderPassDesc.depthStencilAttachment = &depthAttachment;
    }

    // Record commands
    auto renderPass = encoder->beginRenderPass(renderPassDesc);
    if (renderPass) {
        renderPass->setViewport(0.0f, 0.0f,
            static_cast<float>(renderPassDesc.width),
            static_cast<float>(renderPassDesc.height),
            0.0f, 1.0f);
        renderPass->setScissorRect(0, 0, renderPassDesc.width, renderPassDesc.height);

        // Bind pipeline (if created)
        if (rhiPipeline) {
            renderPass->setPipeline(rhiPipeline.get());

            // Bind descriptor sets (bind groups)
            if (frameIndex < rhiBindGroups.size() && rhiBindGroups[frameIndex]) {
                renderPass->setBindGroup(0, rhiBindGroups[frameIndex].get());
            }

            // Phase 4.5: Bind vertex/index buffers and draw
            if (rhiVertexBuffer && rhiIndexBuffer && rhiIndexCount > 0) {
                renderPass->setVertexBuffer(0, rhiVertexBuffer.get(), 0);
                renderPass->setIndexBuffer(rhiIndexBuffer.get(), rhi::IndexFormat::Uint32, 0);
                renderPass->drawIndexed(rhiIndexCount, 1, 0, 0, 0);
            }
        }

        // Phase 7: Render ImGui UI (if initialized)
        if (imguiManager) {
            uint32_t imageIndex = rhiBridge->getCurrentImageIndex();
            imguiManager->render(encoder.get(), imageIndex);
        }

        renderPass->end();
    }

    // Finish command buffer
    auto commandBuffer = encoder->finish();

    // Step 4: Submit command buffer with synchronization
    if (commandBuffer) {
        rhiBridge->submitCommandBuffer(
            commandBuffer.get(),
            rhiBridge->getImageAvailableSemaphore(),
            rhiBridge->getRenderFinishedSemaphore(),
            rhiBridge->getInFlightFence()
        );
    }

    // Step 5: Present frame
    rhiBridge->endFrame();
}
