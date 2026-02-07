#include "Renderer.hpp"
#ifndef __EMSCRIPTEN__
#include "src/ui/ImGuiManager.hpp"
#endif
#include "InstancedRenderData.hpp"
#include "src/utils/Logger.hpp"
#include "src/utils/FileUtils.hpp"

// Phase 9: Vulkan-specific includes for platform-specific functionality
// TODO Phase 10: Consider adding getRenderPass() to RHI interface to remove this last dependency
#ifndef __EMSCRIPTEN__
#include <rhi/vulkan/VulkanRHISwapchain.hpp>
#include <rhi/vulkan/VulkanRHICommandEncoder.hpp>
#include <rhi/vulkan/VulkanRHITexture.hpp>
#include <rhi/vulkan/VulkanRHIBuffer.hpp>
#endif

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

    // Phase 1.2: Initialize IBL (must be before building pipeline for bind group layout)
    createIBL();

    // Always create building pipeline for game world rendering
    createBuildingPipeline();

    // Phase 2.2: Create GPU frustum culling pipeline
    createCullingPipeline();

    // Phase 3.2: Async compute setup
    {
        const auto& features = rhiBridge->getDevice()->getCapabilities().getFeatures();
        if (features.dedicatedComputeQueue && features.timelineSemaphores) {
            computeTimelineSemaphore = rhiBridge->getDevice()->createTimelineSemaphore(0);
            if (computeTimelineSemaphore) {
                useAsyncCompute = true;
                LOG_INFO("Renderer") << "Async compute enabled (dedicated compute queue + timeline semaphores)";
            }
        }
        if (!useAsyncCompute) {
            LOG_INFO("Renderer") << "Async compute disabled, using inline compute on graphics queue";
        }
    }

    // Create particle renderer
    createParticleRenderer();

    // Phase 3.3: Create skybox renderer
    createSkyboxRenderer();

    // Phase 3.3: Create shadow renderer
    createShadowRenderer();

    // Phase 3.1: Log GPU memory statistics
    rhiBridge->getDevice()->logMemoryStats();
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

void Renderer::updateCamera(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& position) {
    viewMatrix = view;
    projectionMatrix = projection;
    cameraPosition = position;
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
#ifndef __EMSCRIPTEN__
    if (imguiManager) {
        imguiManager->handleResize();
    }
#endif
}

void Renderer::initImGui(GLFWwindow* window) {
#ifndef __EMSCRIPTEN__
    // Phase 6: Create ImGui manager using RHI types
    auto* rhiDevice = rhiBridge->getDevice();
    auto* rhiSwapchain = rhiBridge->getSwapchain();

    if (rhiDevice && rhiSwapchain) {
        imguiManager = std::make_unique<ImGuiManager>(window, rhiDevice, rhiSwapchain);
    }
#else
    (void)window;  // Suppress unused parameter warning
#endif
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
    depthDesc.transient = true;  // Phase 3.1: Depth buffer is frame-temporary, enable lazily allocated memory
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
        bufferDesc.mappedAtCreation = false;  // Use write() for updates, not mapping
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
        rhi::VertexAttribute(1, 0, rhi::TextureFormat::RGB32Float, offsetof(Vertex, normal)),  // normal
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
    pipelineDesc.primitive.frontFace = rhi::FrontFace::Clockwise;  // Cube mesh uses CW winding

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

    // Create building shaders
#ifdef __EMSCRIPTEN__
    // WebGPU/Emscripten: Load WGSL shader
    auto wgslCodeRaw = FileUtils::readFile("shaders/building.wgsl");
    if (!wgslCodeRaw.empty()) {
        std::vector<uint8_t> wgslCode(wgslCodeRaw.begin(), wgslCodeRaw.end());

        rhi::ShaderSource vertSource(rhi::ShaderLanguage::WGSL, wgslCode, rhi::ShaderStage::Vertex, "vs_main");
        rhi::ShaderDesc vertDesc(vertSource, "BuildingVertexShader");
        buildingVertexShader = rhiBridge->getDevice()->createShader(vertDesc);

        rhi::ShaderSource fragSource(rhi::ShaderLanguage::WGSL, wgslCode, rhi::ShaderStage::Fragment, "fs_main");
        rhi::ShaderDesc fragDesc(fragSource, "BuildingFragmentShader");
        buildingFragmentShader = rhiBridge->getDevice()->createShader(fragDesc);
    }
    LOG_DEBUG("Renderer") << "Using building shaders (WGSL)";
#else
    // Vulkan/Native: Load SPIR-V shaders
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
    LOG_DEBUG("Renderer") << "Using building shaders (SPIR-V)";
#endif

    if (!buildingVertexShader || !buildingFragmentShader) {
        LOG_ERROR("Renderer") << "Failed to create building shaders";
        return;
    }

    // Create dedicated bind group layout for buildings (UBO + shadow map)
    rhi::BindGroupLayoutDesc buildingLayoutDesc;

    // Binding 0: Uniform buffer (vertex + fragment)
    rhi::BindGroupLayoutEntry uboEntry;
    uboEntry.binding = 0;
    uboEntry.visibility = rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment;
    uboEntry.type = rhi::BindingType::UniformBuffer;
    buildingLayoutDesc.entries.push_back(uboEntry);

    // Binding 1: Shadow map texture (fragment only) - depth texture for shadow mapping
    rhi::BindGroupLayoutEntry shadowTexEntry;
    shadowTexEntry.binding = 1;
    shadowTexEntry.visibility = rhi::ShaderStage::Fragment;
    shadowTexEntry.type = rhi::BindingType::DepthTexture;
    buildingLayoutDesc.entries.push_back(shadowTexEntry);

    // Binding 2: Shadow sampler (fragment only) - non-filtering for depth texture
    rhi::BindGroupLayoutEntry shadowSamplerEntry;
    shadowSamplerEntry.binding = 2;
    shadowSamplerEntry.visibility = rhi::ShaderStage::Fragment;
    shadowSamplerEntry.type = rhi::BindingType::NonFilteringSampler;
    buildingLayoutDesc.entries.push_back(shadowSamplerEntry);

    // Binding 3: IBL Irradiance cubemap (fragment only)
    rhi::BindGroupLayoutEntry irrEntry;
    irrEntry.binding = 3;
    irrEntry.visibility = rhi::ShaderStage::Fragment;
    irrEntry.type = rhi::BindingType::SampledTexture;
    irrEntry.textureViewDimension = rhi::TextureViewDimension::ViewCube;
    buildingLayoutDesc.entries.push_back(irrEntry);

    // Binding 4: IBL Prefiltered env cubemap (fragment only)
    rhi::BindGroupLayoutEntry prefiltEntry;
    prefiltEntry.binding = 4;
    prefiltEntry.visibility = rhi::ShaderStage::Fragment;
    prefiltEntry.type = rhi::BindingType::SampledTexture;
    prefiltEntry.textureViewDimension = rhi::TextureViewDimension::ViewCube;
    buildingLayoutDesc.entries.push_back(prefiltEntry);

    // Binding 5: IBL BRDF LUT (fragment only)
    rhi::BindGroupLayoutEntry brdfEntry;
    brdfEntry.binding = 5;
    brdfEntry.visibility = rhi::ShaderStage::Fragment;
    brdfEntry.type = rhi::BindingType::SampledTexture;
    buildingLayoutDesc.entries.push_back(brdfEntry);

    // Binding 6: IBL sampler (fragment only)
    rhi::BindGroupLayoutEntry iblSamplerEntry;
    iblSamplerEntry.binding = 6;
    iblSamplerEntry.visibility = rhi::ShaderStage::Fragment;
    iblSamplerEntry.type = rhi::BindingType::Sampler;
    buildingLayoutDesc.entries.push_back(iblSamplerEntry);

    buildingLayoutDesc.label = "Building Bind Group Layout";

    buildingBindGroupLayout = rhiBridge->getDevice()->createBindGroupLayout(buildingLayoutDesc);

    if (!buildingBindGroupLayout) {
        LOG_ERROR("Renderer") << "Failed to create building bind group layout";
        return;
    }

    // Note: Bind groups will be created/updated in createShadowRenderer() after shadow renderer is ready
    buildingBindGroups.clear();
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        buildingBindGroups.push_back(nullptr);
    }

    // Phase 2.1+2.2: Create SSBO bind group layout (set 1) for per-object data + visible indices
    {
        rhi::BindGroupLayoutDesc ssboLayoutDesc;

        rhi::BindGroupLayoutEntry ssboEntry;
        ssboEntry.binding = 0;
        ssboEntry.visibility = rhi::ShaderStage::Vertex;
        ssboEntry.type = rhi::BindingType::StorageBuffer;
        ssboLayoutDesc.entries.push_back(ssboEntry);

        // Phase 2.2: Visible indices buffer for frustum culling indirection
        rhi::BindGroupLayoutEntry visibleIndicesEntry;
        visibleIndicesEntry.binding = 1;
        visibleIndicesEntry.visibility = rhi::ShaderStage::Vertex;
        visibleIndicesEntry.type = rhi::BindingType::StorageBuffer;
        ssboLayoutDesc.entries.push_back(visibleIndicesEntry);

        ssboLayoutDesc.label = "SSBO Bind Group Layout";
        ssboBindGroupLayout = rhiBridge->getDevice()->createBindGroupLayout(ssboLayoutDesc);

        if (!ssboBindGroupLayout) {
            LOG_ERROR("Renderer") << "Failed to create SSBO bind group layout";
            return;
        }
    }

    // Create pipeline layout with two bind group layouts: set 0 (UBO+textures), set 1 (SSBO)
    rhi::PipelineLayoutDesc layoutDesc;
    layoutDesc.bindGroupLayouts.push_back(buildingBindGroupLayout.get());
    layoutDesc.bindGroupLayouts.push_back(ssboBindGroupLayout.get());
    buildingPipelineLayout = rhiBridge->createPipelineLayout(layoutDesc);

    if (!buildingPipelineLayout) {
        LOG_ERROR("Renderer") << "Failed to create building pipeline layout";
        return;
    }

    // Setup vertex state - per-vertex attributes only (binding 0)
    // Phase 2.1: Instance data now comes from SSBO, not vertex attributes
    rhi::VertexBufferLayout vertexLayout;
    vertexLayout.stride = sizeof(Vertex);
    vertexLayout.inputRate = rhi::VertexInputRate::Vertex;
    vertexLayout.attributes = {
        rhi::VertexAttribute(0, 0, rhi::TextureFormat::RGB32Float, offsetof(Vertex, pos)),    // inPosition
        rhi::VertexAttribute(1, 0, rhi::TextureFormat::RGB32Float, offsetof(Vertex, normal)),  // inNormal
        rhi::VertexAttribute(2, 0, rhi::TextureFormat::RG32Float, offsetof(Vertex, texCoord)) // inTexCoord
    };

    // Create render pipeline descriptor
    rhi::RenderPipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = buildingVertexShader.get();
    pipelineDesc.fragmentShader = buildingFragmentShader.get();
    pipelineDesc.layout = buildingPipelineLayout.get();
    pipelineDesc.vertex.buffers.push_back(vertexLayout);

    // Primitive state
    pipelineDesc.primitive.topology = rhi::PrimitiveTopology::TriangleList;
    pipelineDesc.primitive.cullMode = rhi::CullMode::Back;
    pipelineDesc.primitive.frontFace = rhi::FrontFace::Clockwise;  // Cube mesh uses CW winding

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
// Phase 3.3: Skybox Renderer Creation
// ============================================================================

void Renderer::createSkyboxRenderer() {
    if (!rhiBridge || !rhiBridge->isReady()) {
        return;
    }

    auto* rhiDevice = rhiBridge->getDevice();
    auto* rhiQueue = rhiBridge->getGraphicsQueue();
    auto* swapchain = rhiBridge->getSwapchain();

    if (!rhiDevice || !rhiQueue || !swapchain) {
        return;
    }

    // Create skybox renderer
    skyboxRenderer = std::make_unique<rendering::SkyboxRenderer>(rhiDevice, rhiQueue);

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

    if (skyboxRenderer->initialize(colorFormat, depthFormat, nativeRenderPass)) {
        LOG_INFO("Renderer") << "Skybox renderer initialized successfully";
    } else {
        LOG_ERROR("Renderer") << "Failed to initialize skybox renderer";
        skyboxRenderer.reset();
    }
}

void Renderer::createShadowRenderer() {
    if (!rhiBridge || !rhiBridge->isReady()) {
        return;
    }

    auto* rhiDevice = rhiBridge->getDevice();
    auto* rhiQueue = rhiBridge->getGraphicsQueue();

    if (!rhiDevice || !rhiQueue) {
        return;
    }

    // Create shadow renderer
    shadowRenderer = std::make_unique<rendering::ShadowRenderer>(rhiDevice, rhiQueue);

    // Initialize shadow renderer (no native render pass needed - creates its own)
    if (shadowRenderer->initialize(nullptr, ssboBindGroupLayout.get())) {
        LOG_INFO("Renderer") << "Shadow renderer initialized successfully";

        // Update building bind groups with shadow map
        if (buildingBindGroupLayout && shadowRenderer->getShadowMapView() && shadowRenderer->getShadowSampler()) {
            buildingBindGroups.clear();
            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
                rhi::BindGroupDesc bindGroupDesc;
                bindGroupDesc.layout = buildingBindGroupLayout.get();
                bindGroupDesc.entries.push_back(
                    rhi::BindGroupEntry::Buffer(0, rhiUniformBuffers[i].get())
                );
                bindGroupDesc.entries.push_back(
                    rhi::BindGroupEntry::TextureView(1, shadowRenderer->getShadowMapView())
                );
                bindGroupDesc.entries.push_back(
                    rhi::BindGroupEntry::Sampler(2, shadowRenderer->getShadowSampler())
                );
                // IBL bindings (3-6)
                if (iblManager && iblManager->isInitialized()) {
                    bindGroupDesc.entries.push_back(
                        rhi::BindGroupEntry::TextureView(3, iblManager->getIrradianceView())
                    );
                    bindGroupDesc.entries.push_back(
                        rhi::BindGroupEntry::TextureView(4, iblManager->getPrefilteredView())
                    );
                    bindGroupDesc.entries.push_back(
                        rhi::BindGroupEntry::TextureView(5, iblManager->getBRDFLutView())
                    );
                    bindGroupDesc.entries.push_back(
                        rhi::BindGroupEntry::Sampler(6, iblManager->getSampler())
                    );
                }
                bindGroupDesc.label = "Building Bind Group with Shadow + IBL";
                buildingBindGroups.push_back(rhiBridge->getDevice()->createBindGroup(bindGroupDesc));
            }
            LOG_INFO("Renderer") << "Building bind groups updated with shadow map";
        }
    } else {
        LOG_ERROR("Renderer") << "Failed to initialize shadow renderer";
        shadowRenderer.reset();
    }
}

void Renderer::createIBL() {
    if (!rhiBridge || !rhiBridge->isReady()) {
        return;
    }

    auto* rhiDevice = rhiBridge->getDevice();
    auto* rhiQueue = rhiBridge->getGraphicsQueue();

    if (!rhiDevice || !rhiQueue) {
        return;
    }

    iblManager = std::make_unique<rendering::IBLManager>(rhiDevice, rhiQueue);

    // For now, initialize with default (BRDF LUT only, no HDR env map yet)
    if (iblManager->initializeDefault()) {
        LOG_INFO("Renderer") << "IBL manager initialized (default mode)";
    } else {
        LOG_ERROR("Renderer") << "Failed to initialize IBL manager";
        iblManager.reset();
    }
}

bool Renderer::loadEnvironmentMap(const std::string& hdrPath) {
    if (!resourceManager || !iblManager) {
        LOG_ERROR("Renderer") << "Cannot load environment map: missing managers";
        return false;
    }

    // Load HDR texture
    rhi::RHITexture* hdrTexture = nullptr;
    try {
        hdrTexture = resourceManager->loadHDRTexture(hdrPath);
    } catch (const std::exception& e) {
        LOG_ERROR("Renderer") << "Failed to load HDR texture: " << e.what();
        return false;
    }

    if (!hdrTexture) {
        LOG_ERROR("Renderer") << "HDR texture is null";
        return false;
    }

    // Re-initialize IBL with the HDR environment
    iblManager = std::make_unique<rendering::IBLManager>(
        rhiBridge->getDevice(), rhiBridge->getGraphicsQueue());

    if (!iblManager->initialize(hdrTexture)) {
        LOG_ERROR("Renderer") << "Failed to initialize IBL with environment map";
        return false;
    }

    // Rebuild building bind groups with new IBL textures
    if (shadowRenderer && shadowRenderer->getShadowMapView() && shadowRenderer->getShadowSampler()) {
        buildingBindGroups.clear();
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            rhi::BindGroupDesc bindGroupDesc;
            bindGroupDesc.layout = buildingBindGroupLayout.get();
            bindGroupDesc.entries.push_back(
                rhi::BindGroupEntry::Buffer(0, rhiUniformBuffers[i].get())
            );
            bindGroupDesc.entries.push_back(
                rhi::BindGroupEntry::TextureView(1, shadowRenderer->getShadowMapView())
            );
            bindGroupDesc.entries.push_back(
                rhi::BindGroupEntry::Sampler(2, shadowRenderer->getShadowSampler())
            );
            bindGroupDesc.entries.push_back(
                rhi::BindGroupEntry::TextureView(3, iblManager->getIrradianceView())
            );
            bindGroupDesc.entries.push_back(
                rhi::BindGroupEntry::TextureView(4, iblManager->getPrefilteredView())
            );
            bindGroupDesc.entries.push_back(
                rhi::BindGroupEntry::TextureView(5, iblManager->getBRDFLutView())
            );
            bindGroupDesc.entries.push_back(
                rhi::BindGroupEntry::Sampler(6, iblManager->getSampler())
            );
            bindGroupDesc.label = "Building Bind Group with IBL";
            buildingBindGroups.push_back(rhiBridge->getDevice()->createBindGroup(bindGroupDesc));
        }
    }

    // Set skybox environment map
    if (skyboxRenderer) {
        skyboxRenderer->setEnvironmentMap(iblManager->getEnvironmentView(), iblManager->getSampler());
    }

    LOG_INFO("Renderer") << "Environment map loaded: " << hdrPath;
    return true;
}

// ============================================================================
// Phase 2.2: GPU Frustum Culling Pipeline
// ============================================================================

void Renderer::createCullingPipeline() {
    auto* device = rhiBridge->getDevice();

    // Load compute shader
#ifdef __EMSCRIPTEN__
    std::string path = "shaders/frustum_cull.comp.wgsl";
    auto codeRaw = FileUtils::readFile(path);
    if (codeRaw.empty()) {
        LOG_ERROR("Renderer") << "Failed to load " << path;
        return;
    }
    std::vector<uint8_t> code(codeRaw.begin(), codeRaw.end());
    rhi::ShaderSource source(rhi::ShaderLanguage::WGSL, code, rhi::ShaderStage::Compute, "main");
#else
    std::string path = "shaders/frustum_cull.comp.spv";
    auto codeRaw = FileUtils::readFile(path);
    if (codeRaw.empty()) {
        LOG_ERROR("Renderer") << "Failed to load " << path;
        return;
    }
    std::vector<uint8_t> code(codeRaw.begin(), codeRaw.end());
    rhi::ShaderSource source(rhi::ShaderLanguage::SPIRV, code, rhi::ShaderStage::Compute, "main");
#endif

    rhi::ShaderDesc shaderDesc(source, "frustum_cull_compute");
    cullComputeShader = device->createShader(shaderDesc);
    if (!cullComputeShader) {
        LOG_ERROR("Renderer") << "Failed to create frustum cull compute shader";
        return;
    }

    // Create cull bind group layout (4 entries, all Compute visibility)
    rhi::BindGroupLayoutDesc cullLayoutDesc;

    // Binding 0: CullUBO (uniform)
    rhi::BindGroupLayoutEntry cullUboEntry;
    cullUboEntry.binding = 0;
    cullUboEntry.visibility = rhi::ShaderStage::Compute;
    cullUboEntry.type = rhi::BindingType::UniformBuffer;
    cullLayoutDesc.entries.push_back(cullUboEntry);

    // Binding 1: ObjectData[] (storage, read)
    rhi::BindGroupLayoutEntry objEntry;
    objEntry.binding = 1;
    objEntry.visibility = rhi::ShaderStage::Compute;
    objEntry.type = rhi::BindingType::StorageBuffer;
    cullLayoutDesc.entries.push_back(objEntry);

    // Binding 2: IndirectDrawCommand (storage, read_write)
    rhi::BindGroupLayoutEntry indirectEntry;
    indirectEntry.binding = 2;
    indirectEntry.visibility = rhi::ShaderStage::Compute;
    indirectEntry.type = rhi::BindingType::StorageBuffer;
    cullLayoutDesc.entries.push_back(indirectEntry);

    // Binding 3: VisibleIndices[] (storage, write)
    rhi::BindGroupLayoutEntry visIndicesEntry;
    visIndicesEntry.binding = 3;
    visIndicesEntry.visibility = rhi::ShaderStage::Compute;
    visIndicesEntry.type = rhi::BindingType::StorageBuffer;
    cullLayoutDesc.entries.push_back(visIndicesEntry);

    cullLayoutDesc.label = "Cull Bind Group Layout";
    cullBindGroupLayout = device->createBindGroupLayout(cullLayoutDesc);
    if (!cullBindGroupLayout) {
        LOG_ERROR("Renderer") << "Failed to create cull bind group layout";
        return;
    }

    // Create pipeline layout
    rhi::PipelineLayoutDesc plDesc;
    plDesc.bindGroupLayouts = {cullBindGroupLayout.get()};
    cullPipelineLayout = device->createPipelineLayout(plDesc);
    if (!cullPipelineLayout) {
        LOG_ERROR("Renderer") << "Failed to create cull pipeline layout";
        return;
    }

    // Create compute pipeline
    rhi::ComputePipelineDesc cpDesc(cullComputeShader.get(), cullPipelineLayout.get());
    cpDesc.label = "Frustum_Cull_Pipeline";
    cullPipeline = device->createComputePipeline(cpDesc);
    if (!cullPipeline) {
        LOG_ERROR("Renderer") << "Failed to create frustum cull compute pipeline";
        return;
    }

    // Create per-frame buffers
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        // CullUBO: 112 bytes (6 * vec4 + objectCount + indexCount + pad)
        rhi::BufferDesc uboDesc;
        uboDesc.size = sizeof(CullUBO);
        uboDesc.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::MapWrite;
        uboDesc.label = "Cull UBO";
        cullUniformBuffers[i] = device->createBuffer(uboDesc);

        // Indirect draw buffer: 20 bytes (DrawIndexedIndirectCommand)
        // Phase 3.2: Enable concurrent sharing for async compute
        const auto& features = device->getCapabilities().getFeatures();
        bool needsConcurrent = features.dedicatedComputeQueue && features.timelineSemaphores;

        rhi::BufferDesc indirectDesc;
        indirectDesc.size = 20;  // 5 * uint32_t
        indirectDesc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Indirect | rhi::BufferUsage::MapWrite;
        indirectDesc.label = "Indirect Draw Buffer";
        indirectDesc.concurrentSharing = needsConcurrent;
        indirectDrawBuffers[i] = device->createBuffer(indirectDesc);

        // Visible indices buffer: 4 bytes per object
        rhi::BufferDesc visDesc;
        visDesc.size = sizeof(uint32_t) * MAX_CULL_OBJECTS;
        visDesc.usage = rhi::BufferUsage::Storage;
        visDesc.label = "Visible Indices Buffer";
        visDesc.concurrentSharing = needsConcurrent;
        visibleIndicesBuffers[i] = device->createBuffer(visDesc);
    }

    LOG_INFO("Renderer") << "GPU frustum culling pipeline created";
}

void Renderer::extractFrustumPlanes(const glm::mat4& vp, glm::vec4 planes[6]) {
    // Griggs-Hartmann frustum plane extraction from VP matrix
    // GLM is column-major: vp[col][row]
    // Left
    planes[0] = glm::vec4(
        vp[0][3] + vp[0][0],
        vp[1][3] + vp[1][0],
        vp[2][3] + vp[2][0],
        vp[3][3] + vp[3][0]
    );
    // Right
    planes[1] = glm::vec4(
        vp[0][3] - vp[0][0],
        vp[1][3] - vp[1][0],
        vp[2][3] - vp[2][0],
        vp[3][3] - vp[3][0]
    );
    // Bottom
    planes[2] = glm::vec4(
        vp[0][3] + vp[0][1],
        vp[1][3] + vp[1][1],
        vp[2][3] + vp[2][1],
        vp[3][3] + vp[3][1]
    );
    // Top
    planes[3] = glm::vec4(
        vp[0][3] - vp[0][1],
        vp[1][3] - vp[1][1],
        vp[2][3] - vp[2][1],
        vp[3][3] - vp[3][1]
    );
    // Near
    planes[4] = glm::vec4(
        vp[0][3] + vp[0][2],
        vp[1][3] + vp[1][2],
        vp[2][3] + vp[2][2],
        vp[3][3] + vp[3][2]
    );
    // Far
    planes[5] = glm::vec4(
        vp[0][3] - vp[0][2],
        vp[1][3] - vp[1][2],
        vp[2][3] - vp[2][2],
        vp[3][3] - vp[3][2]
    );

    // Normalize each plane
    for (int i = 0; i < 6; i++) {
        float len = glm::length(glm::vec3(planes[i]));
        if (len > 0.0f) {
            planes[i] /= len;
        }
    }
}

void Renderer::performFrustumCulling(rhi::RHICommandEncoder* encoder, uint32_t frameIndex,
                                     uint32_t objectCount, uint32_t indexCount) {
    if (!cullPipeline || objectCount == 0) return;

    auto* objectBuffer = pendingInstancedData->objectBuffer;

    // Step 1: Write CullUBO (frustum planes, objectCount, indexCount)
    CullUBO cullUbo;
    glm::mat4 vp = projectionMatrix * viewMatrix;
    extractFrustumPlanes(vp, cullUbo.frustumPlanes);
    cullUbo.objectCount = objectCount;
    cullUbo.indexCount = indexCount;
    cullUbo.pad[0] = 0;
    cullUbo.pad[1] = 0;
    cullUniformBuffers[frameIndex]->write(&cullUbo, sizeof(CullUBO));

    // Step 2: Reset indirect draw buffer (instanceCount = 0, indexCount = mesh indexCount)
    struct DrawIndexedIndirectCommand {
        uint32_t indexCount;
        uint32_t instanceCount;
        uint32_t firstIndex;
        int32_t  vertexOffset;
        uint32_t firstInstance;
    };
    DrawIndexedIndirectCommand cmd;
    cmd.indexCount = indexCount;
    cmd.instanceCount = 0;  // Will be filled by compute shader
    cmd.firstIndex = 0;
    cmd.vertexOffset = 0;
    cmd.firstInstance = 0;
    indirectDrawBuffers[frameIndex]->write(&cmd, sizeof(cmd));

#ifndef __EMSCRIPTEN__
    // Step 3: Vulkan barriers — host writes visible to compute shader
    auto* vulkanEncoder = dynamic_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(encoder);
    if (vulkanEncoder) {
        auto& cmdBuf = vulkanEncoder->getCommandBuffer();

        // Barrier for CullUBO
        auto* vulkanCullUbo = dynamic_cast<RHI::Vulkan::VulkanRHIBuffer*>(cullUniformBuffers[frameIndex].get());
        auto* vulkanIndirect = dynamic_cast<RHI::Vulkan::VulkanRHIBuffer*>(indirectDrawBuffers[frameIndex].get());
        auto* vulkanObjectBuf = dynamic_cast<RHI::Vulkan::VulkanRHIBuffer*>(objectBuffer);

        std::vector<vk::BufferMemoryBarrier> barriers;
        if (vulkanCullUbo) {
            barriers.push_back(vk::BufferMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eHostWrite,
                .dstAccessMask = vk::AccessFlagBits::eUniformRead,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = vulkanCullUbo->getVkBuffer(),
                .offset = 0,
                .size = VK_WHOLE_SIZE
            });
        }
        if (vulkanIndirect) {
            barriers.push_back(vk::BufferMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eHostWrite,
                .dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = vulkanIndirect->getVkBuffer(),
                .offset = 0,
                .size = VK_WHOLE_SIZE
            });
        }
        if (vulkanObjectBuf) {
            barriers.push_back(vk::BufferMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eHostWrite,
                .dstAccessMask = vk::AccessFlagBits::eShaderRead,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = vulkanObjectBuf->getVkBuffer(),
                .offset = 0,
                .size = VK_WHOLE_SIZE
            });
        }

        if (!barriers.empty()) {
            cmdBuf.pipelineBarrier(
                vk::PipelineStageFlagBits::eHost,
                vk::PipelineStageFlagBits::eComputeShader,
                {}, {}, barriers, {}
            );
        }
    }
#endif

    // Step 4: Create/update cull bind group if objectBuffer changed
    if (objectBuffer != cachedObjectBuffers[frameIndex] || !cullBindGroups[frameIndex]) {
        rhi::BindGroupDesc cullBgDesc;
        cullBgDesc.layout = cullBindGroupLayout.get();
        cullBgDesc.entries.push_back(rhi::BindGroupEntry::Buffer(0, cullUniformBuffers[frameIndex].get()));
        cullBgDesc.entries.push_back(rhi::BindGroupEntry::Buffer(1, objectBuffer));
        cullBgDesc.entries.push_back(rhi::BindGroupEntry::Buffer(2, indirectDrawBuffers[frameIndex].get()));
        cullBgDesc.entries.push_back(rhi::BindGroupEntry::Buffer(3, visibleIndicesBuffers[frameIndex].get()));
        cullBgDesc.label = "Cull Bind Group";
        cullBindGroups[frameIndex] = rhiBridge->getDevice()->createBindGroup(cullBgDesc);
    }

    // Step 5: Dispatch compute shader
    auto computePass = encoder->beginComputePass("Frustum_Cull");
    computePass->setPipeline(cullPipeline.get());
    computePass->setBindGroup(0, cullBindGroups[frameIndex].get());
    computePass->dispatch((objectCount + 63) / 64, 1, 1);
    computePass->end();

#ifndef __EMSCRIPTEN__
    // Step 6: Post-compute barriers — compute writes visible to vertex shader + indirect draw
    if (vulkanEncoder) {
        auto& cmdBuf = vulkanEncoder->getCommandBuffer();
        auto* vulkanIndirect = dynamic_cast<RHI::Vulkan::VulkanRHIBuffer*>(indirectDrawBuffers[frameIndex].get());
        auto* vulkanVisibleIndices = dynamic_cast<RHI::Vulkan::VulkanRHIBuffer*>(visibleIndicesBuffers[frameIndex].get());

        std::vector<vk::BufferMemoryBarrier> postBarriers;
        if (vulkanIndirect) {
            postBarriers.push_back(vk::BufferMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eShaderWrite,
                .dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = vulkanIndirect->getVkBuffer(),
                .offset = 0,
                .size = VK_WHOLE_SIZE
            });
        }
        if (vulkanVisibleIndices) {
            postBarriers.push_back(vk::BufferMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eShaderWrite,
                .dstAccessMask = vk::AccessFlagBits::eShaderRead,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = vulkanVisibleIndices->getVkBuffer(),
                .offset = 0,
                .size = VK_WHOLE_SIZE
            });
        }

        if (!postBarriers.empty()) {
            cmdBuf.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eDrawIndirect | vk::PipelineStageFlagBits::eVertexShader,
                {}, {}, postBarriers, {}
            );
        }
    }
#endif
}

void Renderer::performFrustumCullingAsync(uint32_t frameIndex, uint32_t objectCount, uint32_t indexCount) {
    if (!cullPipeline || objectCount == 0 || !useAsyncCompute) return;

    auto* device = rhiBridge->getDevice();
    auto* objectBuffer = pendingInstancedData->objectBuffer;

    // Step 1: Write CullUBO
    CullUBO cullUbo;
    glm::mat4 vp = projectionMatrix * viewMatrix;
    extractFrustumPlanes(vp, cullUbo.frustumPlanes);
    cullUbo.objectCount = objectCount;
    cullUbo.indexCount = indexCount;
    cullUbo.pad[0] = 0;
    cullUbo.pad[1] = 0;
    cullUniformBuffers[frameIndex]->write(&cullUbo, sizeof(CullUBO));

    // Step 2: Reset indirect draw buffer
    struct DrawIndexedIndirectCommand {
        uint32_t indexCount;
        uint32_t instanceCount;
        uint32_t firstIndex;
        int32_t  vertexOffset;
        uint32_t firstInstance;
    };
    DrawIndexedIndirectCommand cmd;
    cmd.indexCount = indexCount;
    cmd.instanceCount = 0;
    cmd.firstIndex = 0;
    cmd.vertexOffset = 0;
    cmd.firstInstance = 0;
    indirectDrawBuffers[frameIndex]->write(&cmd, sizeof(cmd));

    // Step 3: Create/update cull bind group
    if (objectBuffer != cachedObjectBuffers[frameIndex] || !cullBindGroups[frameIndex]) {
        rhi::BindGroupDesc cullBgDesc;
        cullBgDesc.layout = cullBindGroupLayout.get();
        cullBgDesc.entries.push_back(rhi::BindGroupEntry::Buffer(0, cullUniformBuffers[frameIndex].get()));
        cullBgDesc.entries.push_back(rhi::BindGroupEntry::Buffer(1, objectBuffer));
        cullBgDesc.entries.push_back(rhi::BindGroupEntry::Buffer(2, indirectDrawBuffers[frameIndex].get()));
        cullBgDesc.entries.push_back(rhi::BindGroupEntry::Buffer(3, visibleIndicesBuffers[frameIndex].get()));
        cullBgDesc.label = "Cull Bind Group";
        cullBindGroups[frameIndex] = device->createBindGroup(cullBgDesc);
    }

    // Step 4: Create compute command encoder from compute pool
    auto computeEncoder = device->createCommandEncoder(rhi::QueueType::Compute);
    if (!computeEncoder) return;

#ifndef __EMSCRIPTEN__
    // Pre-compute barriers: host writes visible to compute shader
    auto* vulkanEncoder = dynamic_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(computeEncoder.get());
    if (vulkanEncoder) {
        auto& cmdBuf = vulkanEncoder->getCommandBuffer();
        auto* vulkanCullUbo = dynamic_cast<RHI::Vulkan::VulkanRHIBuffer*>(cullUniformBuffers[frameIndex].get());
        auto* vulkanIndirect = dynamic_cast<RHI::Vulkan::VulkanRHIBuffer*>(indirectDrawBuffers[frameIndex].get());
        auto* vulkanObjectBuf = dynamic_cast<RHI::Vulkan::VulkanRHIBuffer*>(objectBuffer);

        std::vector<vk::BufferMemoryBarrier> barriers;
        if (vulkanCullUbo) {
            barriers.push_back(vk::BufferMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eHostWrite,
                .dstAccessMask = vk::AccessFlagBits::eUniformRead,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = vulkanCullUbo->getVkBuffer(),
                .offset = 0, .size = VK_WHOLE_SIZE
            });
        }
        if (vulkanIndirect) {
            barriers.push_back(vk::BufferMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eHostWrite,
                .dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = vulkanIndirect->getVkBuffer(),
                .offset = 0, .size = VK_WHOLE_SIZE
            });
        }
        if (vulkanObjectBuf) {
            barriers.push_back(vk::BufferMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eHostWrite,
                .dstAccessMask = vk::AccessFlagBits::eShaderRead,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = vulkanObjectBuf->getVkBuffer(),
                .offset = 0, .size = VK_WHOLE_SIZE
            });
        }
        if (!barriers.empty()) {
            cmdBuf.pipelineBarrier(
                vk::PipelineStageFlagBits::eHost,
                vk::PipelineStageFlagBits::eComputeShader,
                {}, {}, barriers, {}
            );
        }
    }
#endif

    // Step 5: Dispatch compute shader
    auto computePass = computeEncoder->beginComputePass("Async_Frustum_Cull");
    computePass->setPipeline(cullPipeline.get());
    computePass->setBindGroup(0, cullBindGroups[frameIndex].get());
    computePass->dispatch((objectCount + 63) / 64, 1, 1);
    computePass->end();

    // No post-compute barriers needed — concurrent sharing mode handles visibility
    // Timeline semaphore provides execution ordering

    // Step 6: Submit to compute queue with timeline signal
    auto computeCmdBuffer = computeEncoder->finish();
    if (computeCmdBuffer) {
        rhi::SubmitInfo computeSubmit;
        computeSubmit.commandBuffers.push_back(computeCmdBuffer.get());
        computeSubmit.timelineSignals.push_back(
            rhi::TimelineSignal{computeTimelineSemaphore.get(), ++computeTimelineValue});

        auto* computeQueue = device->getQueue(rhi::QueueType::Compute);
        computeQueue->submit(computeSubmit);
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

    // Phase 3.3: Lighting parameters
    ubo.sunDirection = sunDirection;
    ubo.sunIntensity = sunIntensity;
    ubo.sunColor = sunColor;
    ubo.ambientIntensity = ambientIntensity;
    ubo.cameraPos = cameraPosition;
    ubo.exposure = exposure;

    // Phase 3.3: Shadow mapping parameters
    if (shadowRenderer && shadowRenderer->isInitialized()) {
        ubo.lightSpaceMatrix = shadowRenderer->getLightSpaceMatrix();
    } else {
        ubo.lightSpaceMatrix = glm::mat4(1.0f);
    }
    ubo.shadowMapSize = glm::vec2(rendering::ShadowRenderer::SHADOW_MAP_SIZE);
    ubo.shadowBias = shadowBias;
    ubo.shadowStrength = shadowStrength;

    // Copy to RHI uniform buffer - always use write() to ensure proper flush to GPU
    auto* buffer = rhiUniformBuffers[currentImage].get();
    if (buffer) {
        buffer->write(&ubo, sizeof(ubo));
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

    // Step 2: Update shadow light matrix (before uniform buffer update)
    if (shadowRenderer && shadowRenderer->isInitialized()) {
        // Use fixed scene center at origin - shadows should only depend on sun direction
        // not on camera position. This prevents shadows from shifting when camera moves.
        glm::vec3 sceneCenter = glm::vec3(0.0f, 0.0f, 0.0f);
        float sceneRadius = 200.0f;  // Large enough to cover all buildings in NASDAQ sector
        shadowRenderer->updateLightMatrix(sunDirection, sceneCenter, sceneRadius);
    }

    // Step 3: Update uniform buffer with RHI (includes shadow matrix)
    updateRHIUniformBuffer(frameIndex);

    // Step 4: Create command encoder
    auto encoder = rhiBridge->createCommandEncoder();
    if (!encoder) {
        return;
    }

    // Step 5: SSBO setup + frustum culling + shadow pass
    if (pendingInstancedData && pendingInstancedData->instanceCount > 0) {
        auto* mesh = pendingInstancedData->mesh;
        auto* objectBuffer = pendingInstancedData->objectBuffer;

        if (mesh && mesh->hasData() && objectBuffer) {
            // Phase 2.1+2.2: Create/update SSBO bind group if buffer changed
            if (objectBuffer != cachedObjectBuffers[frameIndex]) {
                rhi::BindGroupDesc ssboDesc;
                ssboDesc.layout = ssboBindGroupLayout.get();
                ssboDesc.entries.push_back(rhi::BindGroupEntry::Buffer(0, objectBuffer));
                ssboDesc.entries.push_back(rhi::BindGroupEntry::Buffer(1, visibleIndicesBuffers[frameIndex].get()));
                ssboDesc.label = "SSBO Bind Group";
                ssboBindGroups[frameIndex] = rhiBridge->getDevice()->createBindGroup(ssboDesc);
                cachedObjectBuffers[frameIndex] = objectBuffer;

                // Also invalidate cull bind group since objectBuffer changed
                cullBindGroups[frameIndex].reset();
            }

            // Phase 2.2+3.2: Perform GPU frustum culling
            uint32_t instanceCount = pendingInstancedData->instanceCount;
            uint32_t meshIndexCount = static_cast<uint32_t>(mesh->getIndexCount());
            if (useAsyncCompute) {
                // Async: separate compute encoder submitted to compute queue
                performFrustumCullingAsync(frameIndex, instanceCount, meshIndexCount);
            } else {
                // Inline: compute on graphics queue command buffer
                performFrustumCulling(encoder.get(), frameIndex, instanceCount, meshIndexCount);
            }

            // Shadow pass (render scene from light's perspective — uses direct drawIndexed, no culling)
            if (shadowRenderer && shadowRenderer->isInitialized() && instanceCount > 1) {
#ifndef __EMSCRIPTEN__
                // macOS/Windows: Transition shadow map to depth attachment for writing
                auto* vulkanEncoder = dynamic_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(encoder.get());
                auto* shadowTexture = dynamic_cast<RHI::Vulkan::VulkanRHITexture*>(shadowRenderer->getShadowMapTexture());
                if (vulkanEncoder && shadowTexture) {
                    vulkanEncoder->getCommandBuffer().pipelineBarrier(
                        vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eEarlyFragmentTests,
                        vk::PipelineStageFlagBits::eEarlyFragmentTests,
                        {},
                        {},
                        {},
                        vk::ImageMemoryBarrier{
                            .srcAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eDepthStencilAttachmentRead,
                            .dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite,
                            .oldLayout = vk::ImageLayout::eUndefined,
                            .newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
                            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                            .image = shadowTexture->getVkImage(),
                            .subresourceRange = vk::ImageSubresourceRange{
                                .aspectMask = vk::ImageAspectFlagBits::eDepth,
                                .baseMipLevel = 0,
                                .levelCount = 1,
                                .baseArrayLayer = 0,
                                .layerCount = 1
                            }
                        }
                    );
                }
#endif  // !__EMSCRIPTEN__

                auto* shadowPass = shadowRenderer->beginShadowPass(encoder.get(), frameIndex);
                if (shadowPass) {
                    // Bind SSBO (set 1) for per-object data
                    if (ssboBindGroups[frameIndex]) {
                        shadowPass->setBindGroup(1, ssboBindGroups[frameIndex].get());
                    }

                    shadowPass->setVertexBuffer(0, mesh->getVertexBuffer(), 0);
                    shadowPass->setIndexBuffer(mesh->getIndexBuffer(), rhi::IndexFormat::Uint32, 0);

                    // Draw buildings only (skip first instance which is the ground plane)
                    uint32_t buildingCount = instanceCount - 1;
                    shadowPass->drawIndexed(meshIndexCount, buildingCount, 0, 0, 1);

                    shadowRenderer->endShadowPass();

#ifndef __EMSCRIPTEN__
                    // macOS/Windows: Transition shadow map from depth attachment to shader read
                    auto* vulkanEncoderPost = dynamic_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(encoder.get());
                    auto* shadowTexturePost = dynamic_cast<RHI::Vulkan::VulkanRHITexture*>(shadowRenderer->getShadowMapTexture());
                    if (vulkanEncoderPost && shadowTexturePost) {
                        vulkanEncoderPost->getCommandBuffer().pipelineBarrier(
                            vk::PipelineStageFlagBits::eLateFragmentTests,
                            vk::PipelineStageFlagBits::eFragmentShader,
                            {},
                            {},
                            {},
                            vk::ImageMemoryBarrier{
                                .srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite,
                                .dstAccessMask = vk::AccessFlagBits::eShaderRead,
                                .oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
                                .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                .image = shadowTexturePost->getVkImage(),
                                .subresourceRange = vk::ImageSubresourceRange{
                                    .aspectMask = vk::ImageAspectFlagBits::eDepth,
                                    .baseMipLevel = 0,
                                    .levelCount = 1,
                                    .baseArrayLayer = 0,
                                    .layerCount = 1
                                }
                            }
                        );
                    }
#endif
                }
            }
        }
    }

    // Get swapchain view
    auto* swapchainView = rhiBridge->getCurrentSwapchainView();
    if (!swapchainView) {
        return;
    }

#ifndef __EMSCRIPTEN__
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

        // Phase 3.3: Render skybox first (background)
        if (skyboxRenderer) {
            // Calculate inverse view-projection matrix for ray direction
            glm::mat4 viewProj = projectionMatrix * viewMatrix;
            glm::mat4 invViewProj = glm::inverse(viewProj);

            // Calculate elapsed time for animation
            auto currentTime = std::chrono::high_resolution_clock::now();
            float time = std::chrono::duration<float>(currentTime - startTime).count();

            // Update skybox with current sun direction (for sun disk rendering)
            skyboxRenderer->setSunDirection(sunDirection);

            skyboxRenderer->render(renderPass.get(), frameIndex, invViewProj, time);
        }

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

        // Phase 2.1: Render instanced data using SSBO-based pipeline
        if (pendingInstancedData && pendingInstancedData->instanceCount > 0 && buildingPipeline) {
            auto* mesh = pendingInstancedData->mesh;
            auto* objectBuffer = pendingInstancedData->objectBuffer;

            if (mesh && mesh->hasData() && objectBuffer) {
                // Switch to building pipeline
                renderPass->setPipeline(buildingPipeline.get());

                // Bind set 0: UBO + textures
                if (frameIndex < buildingBindGroups.size() && buildingBindGroups[frameIndex]) {
                    renderPass->setBindGroup(0, buildingBindGroups[frameIndex].get());
                }

                // Bind set 1: SSBO with per-object data
                if (ssboBindGroups[frameIndex]) {
                    renderPass->setBindGroup(1, ssboBindGroups[frameIndex].get());
                }

                // Bind vertex buffer only (no instance buffer — data comes from SSBO)
                renderPass->setVertexBuffer(0, mesh->getVertexBuffer(), 0);
                renderPass->setIndexBuffer(mesh->getIndexBuffer(), rhi::IndexFormat::Uint32, 0);

                // Phase 2.2: Draw via indirect buffer (GPU frustum culling sets instanceCount)
                renderPass->drawIndexedIndirect(indirectDrawBuffers[frameIndex].get(), 0);
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
#ifndef __EMSCRIPTEN__
        if (imguiManager) {
            uint32_t imageIndex = rhiBridge->getCurrentImageIndex();
            imguiManager->render(encoder.get(), imageIndex);
        }
#endif

        renderPass->end();
    }

#ifndef __EMSCRIPTEN__
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
        if (useAsyncCompute && computeTimelineValue > 0) {
            // Async compute path: use SubmitInfo with timeline wait
            rhi::SubmitInfo graphicsSubmit;
            graphicsSubmit.commandBuffers.push_back(commandBuffer.get());
            graphicsSubmit.waitSemaphores.push_back(rhiBridge->getImageAvailableSemaphore());
            graphicsSubmit.signalSemaphores.push_back(rhiBridge->getRenderFinishedSemaphore());
            graphicsSubmit.signalFence = rhiBridge->getInFlightFence();
            graphicsSubmit.timelineWaits.push_back(
                rhi::TimelineWait{computeTimelineSemaphore.get(), computeTimelineValue});

            auto* graphicsQueue = rhiBridge->getDevice()->getQueue(rhi::QueueType::Graphics);
            graphicsQueue->submit(graphicsSubmit);
        } else {
            // Inline compute path: simple submit
            rhiBridge->submitCommandBuffer(
                commandBuffer.get(),
                rhiBridge->getImageAvailableSemaphore(),
                rhiBridge->getRenderFinishedSemaphore(),
                rhiBridge->getInFlightFence()
            );
        }
    }

    // Step 5: Present frame
    rhiBridge->endFrame();
}
