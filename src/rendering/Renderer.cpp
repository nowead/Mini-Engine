#include "Renderer.hpp"
#include "src/ui/ImGuiManager.hpp"
#include "InstancedRenderData.hpp"

// Phase 9: Vulkan-specific includes only needed for Linux render pass handle retrieval
// TODO Phase 10: Consider adding getRenderPass() to RHI interface to remove this last dependency
#ifdef __linux__
#include <rhi/vulkan/VulkanRHISwapchain.hpp>
#endif

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

    // Only create main pipeline if we have model data to render
    if (fdfMode) {
        createRHIPipeline();
    }

    // Always create building pipeline for game world rendering
    createBuildingPipeline();
}

Renderer::~Renderer() {
    // Wait for device idle before destroying resources
    if (rhiBridge) {
        rhiBridge->waitIdle();
    }
    // All resources cleaned up by RAII in reverse declaration order
}

void Renderer::loadModel(const std::string& modelPath) {
    currentModelPath = modelPath;
    sceneManager->loadMesh(modelPath, zScale);  // Delegates to SceneManager

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

void Renderer::adjustZScale(float delta) {
    if (!fdfMode || currentModelPath.empty()) {
        return;  // Only works in FDF mode with a loaded model
    }

    zScale += delta;
    zScale = std::max(0.1f, std::min(zScale, 50.0f));  // Clamp between 0.1 and 50.0

    // Wait for device idle before reloading
    rhiBridge->waitIdle();

    // Clear existing meshes and reload with new Z-scale
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

    // Phase 8: Ensure swapchain is created before pipeline (needed for render pass on Linux)
    if (!rhiBridge->getSwapchain()) {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        rhiBridge->createSwapchain(static_cast<uint32_t>(width), static_cast<uint32_t>(height), true);
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

                // Phase 7.5: Wait for device idle to ensure command buffer is fully retired
                // before it's destroyed (prevents "command buffer in use" error)
                rhiDevice->waitIdle();
            }
        }

        std::cout << "[Renderer] RHI buffers uploaded: " 
                  << vertexCount << " vertices (" << vertexBufferSize << " bytes), " 
                  << indexCount << " indices (" << indexBufferSize << " bytes)\n";
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

    if (!buildingVertexShader || !buildingFragmentShader) {
        std::cerr << "[Renderer] Failed to create building shaders\n";
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
        std::cerr << "[Renderer] Failed to create building bind group layout\n";
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
        std::cerr << "[Renderer] Failed to create building pipeline layout\n";
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
    // Must match BuildingManager::InstanceData layout (48 bytes)
    rhi::VertexBufferLayout instanceLayout;
    instanceLayout.stride = 48;  // sizeof(BuildingManager::InstanceData)
    instanceLayout.inputRate = rhi::VertexInputRate::Instance;
    instanceLayout.attributes = {
        rhi::VertexAttribute(3, 1, rhi::TextureFormat::RGB32Float, 0),   // instancePosition (vec3, offset 0)
        rhi::VertexAttribute(4, 1, rhi::TextureFormat::R32Float, 12),    // instanceHeight (float, offset 12)
        rhi::VertexAttribute(5, 1, rhi::TextureFormat::RGBA32Float, 16), // instanceColor (vec4, offset 16)
        rhi::VertexAttribute(6, 1, rhi::TextureFormat::RG32Float, 32)    // instanceBaseScale (vec2, offset 32)
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
        std::cout << "[Renderer] Building instancing pipeline created successfully\n";
    } else {
        std::cerr << "[Renderer] Failed to create building pipeline\n";
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
    auto* swapchain = rhiBridge->getSwapchain();
    if (swapchain) {
        auto* swapchainTexture = swapchain->getCurrentTextureView()->getTexture();
        encoder->transitionTextureLayout(
            swapchainTexture,
            rhi::TextureLayout::Undefined,
            rhi::TextureLayout::ColorAttachment
        );
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
        auto* swapchainTexture = swapchain->getCurrentTextureView()->getTexture();
        encoder->transitionTextureLayout(
            swapchainTexture,
            rhi::TextureLayout::ColorAttachment,
            rhi::TextureLayout::Present
        );
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
