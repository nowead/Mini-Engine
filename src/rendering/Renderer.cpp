#include "Renderer.hpp"
#include "src/ui/ImGuiManager.hpp"
#include "InstancedRenderData.hpp"
#include "src/utils/Logger.hpp"

// Phase 9: Vulkan-specific includes for platform-specific functionality
// TODO Phase 10: Consider adding getRenderPass() to RHI interface to remove this last dependency
#include <rhi/vulkan/VulkanRHISwapchain.hpp>
#include <rhi/vulkan/VulkanRHICommandEncoder.hpp>

#include <stdexcept>

// Phase 7: LegacyCommandBufferAdapter removed - ImGui now uses RHI directly

Renderer::Renderer(GLFWwindow* window,
                   const std::vector<const char*>& validationLayers,
                   bool enableValidation)
    : window(window),
      startTime(std::chrono::high_resolution_clock::now()),
      viewMatrix(glm::mat4(1.0f)),
      projectionMatrix(glm::mat4(1.0f)) {

    // Initialize RHI Bridge (handles device creation, surface, and lifecycle)
    rhiBridge = std::make_unique<rendering::RendererBridge>(window, enableValidation);

    // Create swapchain (needed for depth resources)
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    rhiBridge->createSwapchain(static_cast<uint32_t>(width), static_cast<uint32_t>(height), true);

    // Create high-level managers using RHI
    auto* rhiDevice = rhiBridge->getDevice();
    auto* rhiQueue = rhiDevice->getQueue(rhi::QueueType::Graphics);
    resourceManager = std::make_unique<ResourceManager>(rhiDevice, rhiQueue);
    sceneManager = std::make_unique<SceneManager>(rhiDevice, rhiQueue);

    // Create RHI resources
    createRHIDepthResources();
    createRHIUniformBuffers();
    createRHIBindGroups();

    // Always create building pipeline for game world rendering
    createBuildingPipeline();

    // Create particle renderer
    createParticleRenderer();
}

Renderer::~Renderer() {
    // Wait for device idle before destroying resources
    if (rhiBridge) {
        rhiBridge->waitIdle();
    }
    // All resources cleaned up by RAII in reverse declaration order
}

void Renderer::loadModel(const std::string& modelPath) {
    sceneManager->loadMesh(modelPath);  // Delegates to SceneManager

    // Phase 4.5: Create RHI buffers after loading mesh
    createRHIBuffers();
}

void Renderer::loadTexture(const std::string& texturePath) {
    resourceManager->loadTexture(texturePath);  // Delegates to ResourceManager
    // Descriptor updates handled via RHI bind groups
}

void Renderer::waitIdle() {
    rhiBridge->waitIdle();
}

void Renderer::handleFramebufferResize() {
    recreateSwapchain();
}

void Renderer::updateCamera(const glm::mat4& view, const glm::mat4& projection) {
    viewMatrix = view;
    projectionMatrix = projection;
}

void Renderer::submitInstancedRenderData(const rendering::InstancedRenderData& data) {
    // Store copy of data for this frame (fixes dangling pointer issue)
    pendingInstancedData = data;
}

void Renderer::submitParticleSystem(effects::ParticleSystem* particleSystem) {
    pendingParticleSystem = particleSystem;
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

// Phase 8: All legacy resource creation methods removed - using only RHI

void Renderer::recreateSwapchain() {
    // Wait for window to be visible
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }

    rhiBridge->waitIdle();

    // Recreate RHI swapchain and depth resources
    rhiBridge->createSwapchain(static_cast<uint32_t>(width), static_cast<uint32_t>(height), true);
    createRHIDepthResources();
    createRHIPipeline();  // Pipeline needs recreation with new render pass

    // Notify ImGui of resize
    if (imguiManager) {
        imguiManager->handleResize();
    }
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
    auto* rhiSwapchain = rhiBridge->getSwapchain();
    if (!rhiSwapchain) {
        return;  // Swapchain not created yet
    }

    // Create depth texture using RHI
    rhi::TextureDesc depthDesc;
    depthDesc.size = rhi::Extent3D(rhiSwapchain->getWidth(), rhiSwapchain->getHeight(), 1);
    depthDesc.format = rhi::TextureFormat::Depth32Float;
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
        bufferDesc.size = sizeof(UniformBufferObject);  // model + view + proj
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

    // Binding 1: Combined image sampler
    rhi::BindGroupLayoutEntry samplerEntry;
    samplerEntry.binding = 1;
    samplerEntry.visibility = rhi::ShaderStage::Fragment;
    samplerEntry.type = rhi::BindingType::SampledTexture;
    layoutDesc.entries.push_back(samplerEntry);

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

    // Phase 8: Ensure swapchain is created before pipeline (needed for render pass on Linux)
    if (!rhiBridge->getSwapchain()) {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        rhiBridge->createSwapchain(static_cast<uint32_t>(width), static_cast<uint32_t>(height), true);
    }

    // Select shader path
    std::string shaderPath = "shaders/slang.spv";

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
        LOG_ERROR("Renderer") << "Failed to create RHI shaders";
        return;
    }

    // Create pipeline layout
    rhi::PipelineLayoutDesc layoutDesc;
    layoutDesc.bindGroupLayouts.push_back(rhiBindGroupLayout.get());
    rhiPipelineLayout = rhiBridge->createPipelineLayout(layoutDesc);

    if (!rhiPipelineLayout) {
        LOG_ERROR("Renderer") << "Failed to create RHI pipeline layout";
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

    // Primitive state
    pipelineDesc.primitive.topology = rhi::PrimitiveTopology::TriangleList;
    pipelineDesc.primitive.cullMode = rhi::CullMode::Back;
    pipelineDesc.primitive.frontFace = rhi::FrontFace::CounterClockwise;

    // Depth-stencil state
    rhi::DepthStencilState depthStencilState;
    depthStencilState.depthTestEnabled = true;
    depthStencilState.depthWriteEnabled = true;
    depthStencilState.depthCompare = rhi::CompareOp::Less;
    depthStencilState.format = rhi::TextureFormat::Depth32Float;
    pipelineDesc.depthStencil = &depthStencilState;

    // Phase 7.5: Color target - use actual swapchain format to avoid validation errors
    rhi::ColorTargetState colorTarget;
    auto* swapchain = rhiBridge->getSwapchain();
    if (swapchain) {
        colorTarget.format = swapchain->getFormat();  // Match swapchain format (SRGB or UNORM)
    } else {
        colorTarget.format = rhi::TextureFormat::BGRA8UnormSrgb;  // Default to SRGB
    }
    colorTarget.blend.blendEnabled = false;
    pipelineDesc.colorTargets.push_back(colorTarget);

    pipelineDesc.label = "RHI Main Pipeline";

    // Phase 9: Ensure platform-specific render resources are ready (uses RHI abstraction)
    // - On Linux: Creates traditional render pass and framebuffers
    // - On macOS/Windows: No-op (uses dynamic rendering)
    if (swapchain) {
        swapchain->ensureRenderResourcesReady(rhiDepthImageView.get());

#ifdef __linux__
        // Linux still needs native render pass handle for pipeline creation
        auto* vulkanSwapchain = dynamic_cast<RHI::Vulkan::VulkanRHISwapchain*>(swapchain);
        if (vulkanSwapchain) {
            VkRenderPass vkPass = static_cast<VkRenderPass>(vulkanSwapchain->getRenderPass());
            pipelineDesc.nativeRenderPass = reinterpret_cast<void*>(vkPass);
        }
#endif
    }

    // Create pipeline
    rhiPipeline = rhiBridge->createRenderPipeline(pipelineDesc);

    if (rhiPipeline) {
        LOG_INFO("Renderer") << "RHI Pipeline created successfully";
    } else {
        LOG_ERROR("Renderer") << "Failed to create RHI pipeline";
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

                // Phase 7.5: Wait for device idle to ensure command buffer is fully retired
                // before it's destroyed (prevents "command buffer in use" error)
                rhiDevice->waitIdle();
            }
        }

        LOG_INFO("Renderer") << "RHI buffers uploaded: " 
                  << vertexCount << " vertices (" << vertexBufferSize << " bytes), " 
                  << indexCount << " indices (" << indexBufferSize << " bytes)";
    }
}

// ============================================================================
// Building Instancing Pipeline Creation
// ============================================================================

void Renderer::createBuildingPipeline() {
    if (!rhiBridge || !rhiBridge->isReady() || !rhiBindGroupLayout) {
        return;
    }

    // Ensure swapchain is created
    if (!rhiBridge->getSwapchain()) {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        rhiBridge->createSwapchain(static_cast<uint32_t>(width), static_cast<uint32_t>(height), true);
    }

    // Create building shaders from SPIR-V files
    buildingVertexShader = rhiBridge->createShaderFromFile(
        "shaders/building.vert.spv",
        rhi::ShaderStage::Vertex,
        "main"
    );

    buildingFragmentShader = rhiBridge->createShaderFromFile(
        "shaders/building.frag.spv",
        rhi::ShaderStage::Fragment,
        "main"
    );

    LOG_DEBUG("Renderer") << "Using building shaders (simplified instancing format)";

    if (!buildingVertexShader || !buildingFragmentShader) {
        LOG_ERROR("Renderer") << "Failed to create building shaders";
        return;
    }

    // Create dedicated bind group layout for buildings (only UBO, no texture)
    rhi::BindGroupLayoutDesc buildingLayoutDesc;
    rhi::BindGroupLayoutEntry uboEntry;
    uboEntry.binding = 0;
    uboEntry.visibility = rhi::ShaderStage::Vertex;
    uboEntry.type = rhi::BindingType::UniformBuffer;
    buildingLayoutDesc.entries.push_back(uboEntry);
    buildingLayoutDesc.label = "Building Bind Group Layout";

    buildingBindGroupLayout = rhiBridge->getDevice()->createBindGroupLayout(buildingLayoutDesc);

    if (!buildingBindGroupLayout) {
        LOG_ERROR("Renderer") << "Failed to create building bind group layout";
        return;
    }

    // Create bind groups for building rendering (one per frame)
    buildingBindGroups.clear();
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        rhi::BindGroupDesc bindGroupDesc;
        bindGroupDesc.layout = buildingBindGroupLayout.get();
        bindGroupDesc.entries.push_back(
            rhi::BindGroupEntry::Buffer(0, rhiUniformBuffers[i].get())
        );
        bindGroupDesc.label = "Building Bind Group";
        buildingBindGroups.push_back(rhiBridge->getDevice()->createBindGroup(bindGroupDesc));
    }

    // Create pipeline layout
    rhi::PipelineLayoutDesc layoutDesc;
    layoutDesc.bindGroupLayouts.push_back(buildingBindGroupLayout.get());
    buildingPipelineLayout = rhiBridge->createPipelineLayout(layoutDesc);

    if (!buildingPipelineLayout) {
        LOG_ERROR("Renderer") << "Failed to create building pipeline layout";
        return;
    }

    // Setup vertex state - per-vertex attributes (binding 0)
    rhi::VertexBufferLayout vertexLayout;
    vertexLayout.stride = sizeof(Vertex);
    vertexLayout.inputRate = rhi::VertexInputRate::Vertex;
    vertexLayout.attributes = {
        rhi::VertexAttribute(0, 0, rhi::TextureFormat::RGB32Float, offsetof(Vertex, pos)),    // inPosition
        rhi::VertexAttribute(1, 0, rhi::TextureFormat::RGB32Float, offsetof(Vertex, color)),  // inNormal (reuse color slot)
        rhi::VertexAttribute(2, 0, rhi::TextureFormat::RG32Float, offsetof(Vertex, texCoord)) // inTexCoord
    };

    // Setup instance state - per-instance attributes (binding 1)
    // Use vec3 scale for independent X/Z base size and Y height
    rhi::VertexBufferLayout instanceLayout;
    instanceLayout.stride = 40;  // vec3(12) + vec3(12) + vec3(12) + padding(4) = 40 bytes
    instanceLayout.inputRate = rhi::VertexInputRate::Instance;
    instanceLayout.attributes = {
        rhi::VertexAttribute(3, 1, rhi::TextureFormat::RGB32Float, 0),   // instancePosition (vec3, offset 0)
        rhi::VertexAttribute(4, 1, rhi::TextureFormat::RGB32Float, 12),  // instanceColor (vec3, offset 12)
        rhi::VertexAttribute(5, 1, rhi::TextureFormat::RGB32Float, 24)   // instanceScale (vec3, offset 24)
    };

    // Create render pipeline descriptor
    rhi::RenderPipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = buildingVertexShader.get();
    pipelineDesc.fragmentShader = buildingFragmentShader.get();
    pipelineDesc.layout = buildingPipelineLayout.get();
    pipelineDesc.vertex.buffers.push_back(vertexLayout);
    pipelineDesc.vertex.buffers.push_back(instanceLayout);

    // Primitive state
    pipelineDesc.primitive.topology = rhi::PrimitiveTopology::TriangleList;
    pipelineDesc.primitive.cullMode = rhi::CullMode::Back;
    pipelineDesc.primitive.frontFace = rhi::FrontFace::CounterClockwise;

    // Depth-stencil state
    rhi::DepthStencilState depthStencilState;
    depthStencilState.depthTestEnabled = true;
    depthStencilState.depthWriteEnabled = true;
    depthStencilState.depthCompare = rhi::CompareOp::Less;
    depthStencilState.format = rhi::TextureFormat::Depth32Float;
    pipelineDesc.depthStencil = &depthStencilState;

    // Color target - match swapchain format
    rhi::ColorTargetState colorTarget;
    auto* swapchain = rhiBridge->getSwapchain();
    if (swapchain) {
        colorTarget.format = swapchain->getFormat();
    } else {
        colorTarget.format = rhi::TextureFormat::BGRA8UnormSrgb;
    }
    colorTarget.blend.blendEnabled = false;
    pipelineDesc.colorTargets.push_back(colorTarget);

    pipelineDesc.label = "Building Instancing Pipeline";

    // Ensure platform-specific render resources are ready
    if (swapchain) {
        swapchain->ensureRenderResourcesReady(rhiDepthImageView.get());

#ifdef __linux__
        // Linux needs native render pass handle for pipeline creation
        auto* vulkanSwapchain = dynamic_cast<RHI::Vulkan::VulkanRHISwapchain*>(swapchain);
        if (vulkanSwapchain) {
            VkRenderPass vkPass = static_cast<VkRenderPass>(vulkanSwapchain->getRenderPass());
            pipelineDesc.nativeRenderPass = reinterpret_cast<void*>(vkPass);
        }
#endif
    }

    // Create pipeline
    buildingPipeline = rhiBridge->createRenderPipeline(pipelineDesc);

    if (buildingPipeline) {
        LOG_INFO("Renderer") << "Building instancing pipeline created successfully";
    } else {
        LOG_ERROR("Renderer") << "Failed to create building pipeline";
    }
}

// ============================================================================
// Phase 3.1: Particle Renderer Creation
// ============================================================================

void Renderer::createParticleRenderer() {
    if (!rhiBridge || !rhiBridge->isReady()) {
        return;
    }

    auto* rhiDevice = rhiBridge->getDevice();
    auto* rhiQueue = rhiBridge->getGraphicsQueue();
    auto* swapchain = rhiBridge->getSwapchain();

    if (!rhiDevice || !rhiQueue || !swapchain) {
        return;
    }

    // Create particle renderer
    particleRenderer = std::make_unique<effects::ParticleRenderer>(rhiDevice, rhiQueue);

    // Initialize with swapchain format and depth format
    rhi::TextureFormat colorFormat = swapchain->getFormat();
    rhi::TextureFormat depthFormat = rhi::TextureFormat::Depth32Float;

    // Get native render pass for Linux
    void* nativeRenderPass = nullptr;
#ifdef __linux__
    auto* vulkanSwapchain = dynamic_cast<RHI::Vulkan::VulkanRHISwapchain*>(swapchain);
    if (vulkanSwapchain) {
        nativeRenderPass = vulkanSwapchain->getRenderPass();
    }
#endif

    if (particleRenderer->initialize(colorFormat, depthFormat, nativeRenderPass)) {
        LOG_INFO("Renderer") << "Particle renderer initialized successfully";
    } else {
        LOG_ERROR("Renderer") << "Failed to initialize particle renderer";
        particleRenderer.reset();
    }
}

// ============================================================================
// Phase 8: RHI Uniform Buffer Update
// ============================================================================

void Renderer::updateRHIUniformBuffer(uint32_t currentImage) {
    if (currentImage >= rhiUniformBuffers.size() || !rhiUniformBuffers[currentImage]) {
        return;
    }

    UniformBufferObject ubo{};
    ubo.model = glm::mat4(1.0f);  // Identity matrix (no model transform)
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

    // Ensure render resources (framebuffers) are ready before rendering
    auto* swapchain = rhiBridge->getSwapchain();
    if (swapchain && rhiDepthImageView) {
        swapchain->ensureRenderResourcesReady(rhiDepthImageView.get());
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

#ifndef __linux__
    // Phase 9: Transition swapchain image from UNDEFINED to COLOR_ATTACHMENT_OPTIMAL
    // before starting the render pass (only needed for dynamic rendering on macOS/Windows)
    // Linux uses traditional render pass which handles layout transitions automatically
    if (swapchain) {
        // Use Vulkan-specific method to get current image for layout transition
        auto* vulkanSwapchain = dynamic_cast<RHI::Vulkan::VulkanRHISwapchain*>(swapchain);
        auto* vulkanEncoder = dynamic_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(encoder.get());
        if (vulkanSwapchain && vulkanEncoder) {
            vk::Image swapchainImage = vulkanSwapchain->getCurrentVkImage();
            // Transition from UNDEFINED to COLOR_ATTACHMENT_OPTIMAL
            vulkanEncoder->getCommandBuffer().pipelineBarrier(
                vk::PipelineStageFlagBits::eTopOfPipe,
                vk::PipelineStageFlagBits::eColorAttachmentOutput,
                {},
                {},
                {},
                vk::ImageMemoryBarrier{
                    .srcAccessMask = {},
                    .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
                    .oldLayout = vk::ImageLayout::eUndefined,
                    .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = swapchainImage,
                    .subresourceRange = vk::ImageSubresourceRange{
                        .aspectMask = vk::ImageAspectFlagBits::eColor,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1
                    }
                }
            );
        }
    }
#endif

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
    colorAttachment.clearValue = rhi::ClearColorValue(0.01f, 0.01f, 0.03f, 1.0f);  // Dark blue background
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

    // Phase 8: Linux requires traditional render pass (no dynamic rendering)
#ifdef __linux__
    auto* rhiVulkanSwapchain = dynamic_cast<RHI::Vulkan::VulkanRHISwapchain*>(rhiBridge->getSwapchain());
    if (rhiVulkanSwapchain) {
        uint32_t currentImageIndex = rhiBridge->getCurrentImageIndex();
        VkRenderPass vkPass = static_cast<VkRenderPass>(rhiVulkanSwapchain->getRenderPass());
        VkFramebuffer vkFramebuffer = static_cast<VkFramebuffer>(rhiVulkanSwapchain->getFramebuffer(currentImageIndex));
        renderPassDesc.nativeRenderPass = reinterpret_cast<void*>(vkPass);
        renderPassDesc.nativeFramebuffer = reinterpret_cast<void*>(vkFramebuffer);
    }
#endif

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

        // NEW: Render instanced data (buildings, etc.) using GPU Instancing (Phase 1.1)
        // Clean interface - Renderer doesn't know about game entities
        // NOTE: This is OUTSIDE the rhiPipeline block so buildings render independently
        if (pendingInstancedData && pendingInstancedData->instanceCount > 0 && buildingPipeline) {
            auto* mesh = pendingInstancedData->mesh;
            auto* instanceBuffer = pendingInstancedData->instanceBuffer;

            if (mesh && mesh->hasData() && instanceBuffer) {
                // Switch to building instancing pipeline
                renderPass->setPipeline(buildingPipeline.get());

                // Bind building-specific descriptor sets (UBO only)
                if (frameIndex < buildingBindGroups.size() && buildingBindGroups[frameIndex]) {
                    renderPass->setBindGroup(0, buildingBindGroups[frameIndex].get());
                }

                // Bind vertex buffer (slot 0: per-vertex data)
                renderPass->setVertexBuffer(0, mesh->getVertexBuffer(), 0);

                // Bind instance buffer (slot 1: per-instance data)
                renderPass->setVertexBuffer(1, instanceBuffer, 0);

                // Bind index buffer
                renderPass->setIndexBuffer(mesh->getIndexBuffer(), rhi::IndexFormat::Uint32, 0);

                // Draw all instances in one call!
                renderPass->drawIndexed(
                    static_cast<uint32_t>(mesh->getIndexCount()),      // indexCount
                    pendingInstancedData->instanceCount,                // instanceCount
                    0,                                                  // firstIndex
                    0,                                                  // vertexOffset
                    0                                                   // firstInstance
                );

            }

            // Clear pending data after rendering
            pendingInstancedData.reset();
        }

        // Phase 3.1: Render particles (after opaque geometry, before ImGui)
        if (particleRenderer && pendingParticleSystem) {
            // Update particle renderer camera
            particleRenderer->updateCamera(viewMatrix, projectionMatrix);

            // Render particles
            particleRenderer->render(renderPass.get(), *pendingParticleSystem, frameIndex);

            // Clear pending particle system
            pendingParticleSystem = nullptr;
        }

        // Phase 7: Render ImGui UI (if initialized)
        if (imguiManager) {
            uint32_t imageIndex = rhiBridge->getCurrentImageIndex();
            imguiManager->render(encoder.get(), imageIndex);
        }

        renderPass->end();
    }

#ifndef __linux__
    // Phase 9: Transition swapchain image from COLOR_ATTACHMENT_OPTIMAL to PRESENT_SRC
    // This must be done before finishing the command buffer
    // Only needed for dynamic rendering on macOS/Windows
    // Linux uses traditional render pass which handles layout transitions automatically
    if (swapchain) {
        // Use Vulkan-specific method to get current image for layout transition
        auto* vulkanSwapchain = dynamic_cast<RHI::Vulkan::VulkanRHISwapchain*>(swapchain);
        auto* vulkanEncoder = dynamic_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(encoder.get());
        if (vulkanSwapchain && vulkanEncoder) {
            vk::Image swapchainImage = vulkanSwapchain->getCurrentVkImage();
            // Transition from COLOR_ATTACHMENT_OPTIMAL to PRESENT_SRC
            vulkanEncoder->getCommandBuffer().pipelineBarrier(
                vk::PipelineStageFlagBits::eColorAttachmentOutput,
                vk::PipelineStageFlagBits::eBottomOfPipe,
                {},
                {},
                {},
                vk::ImageMemoryBarrier{
                    .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
                    .dstAccessMask = {},
                    .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
                    .newLayout = vk::ImageLayout::ePresentSrcKHR,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = swapchainImage,
                    .subresourceRange = vk::ImageSubresourceRange{
                        .aspectMask = vk::ImageAspectFlagBits::eColor,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1
                    }
                }
            );
        }
    }
#endif

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
