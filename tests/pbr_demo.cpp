/**
 * @file pbr_demo.cpp
 * @brief PBR/IBL Material Showcase — 7x7 Roughness×Metallic Sphere Grid
 *
 * Demonstrates:
 * - Cook-Torrance PBR (GGX, Smith-Schlick, Fresnel-Schlick)
 * - Image-Based Lighting (Irradiance, Prefiltered Env, BRDF LUT)
 * - 7×7 sphere grid: roughness (X) × metallic (Y)
 * - Large mirror sphere for clear environment reflections
 * - HDR studio environment (ferndale_studio_12_4k.hdr)
 * - Orbit camera controls (left-drag rotate, scroll zoom)
 */

#include <rhi/RHI.hpp>
#include "src/rendering/RendererBridge.hpp"
#include "src/rendering/IBLManager.hpp"
#include "src/rendering/SkyboxRenderer.hpp"
#include "src/resources/ResourceManager.hpp"
#include "src/scene/Camera.hpp"
#include "src/utils/Vertex.hpp"

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <vulkan/vulkan.h>
#include <rhi/vulkan/VulkanRHISwapchain.hpp>
#endif

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

#include <vector>
#include <memory>
#include <iostream>
#include <cmath>

// =============================================================================
// Sphere Mesh Generation
// =============================================================================

struct SphereVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
};

std::vector<SphereVertex> generateSphereVertices(float radius, uint32_t sectorCount, uint32_t stackCount) {
    std::vector<SphereVertex> vertices;
    const float sectorStep = 2.0f * glm::pi<float>() / sectorCount;
    const float stackStep = glm::pi<float>() / stackCount;

    for (uint32_t i = 0; i <= stackCount; ++i) {
        const float stackAngle = glm::pi<float>() / 2.0f - i * stackStep;
        const float xz = radius * std::cos(stackAngle);
        const float y = radius * std::sin(stackAngle);

        for (uint32_t j = 0; j <= sectorCount; ++j) {
            const float sectorAngle = j * sectorStep;
            SphereVertex vertex{};
            vertex.position.x = xz * std::cos(sectorAngle);
            vertex.position.y = y;  // Y-up: poles along Y axis
            vertex.position.z = xz * std::sin(sectorAngle);
            vertex.normal = glm::normalize(vertex.position);
            vertex.texCoord.x = static_cast<float>(j) / sectorCount;
            vertex.texCoord.y = static_cast<float>(i) / stackCount;
            vertices.push_back(vertex);
        }
    }

    return vertices;
}

std::vector<uint32_t> generateSphereIndices(uint32_t sectorCount, uint32_t stackCount) {
    std::vector<uint32_t> indices;

    for (uint32_t i = 0; i < stackCount; ++i) {
        uint32_t k1 = i * (sectorCount + 1);
        uint32_t k2 = k1 + sectorCount + 1;

        for (uint32_t j = 0; j < sectorCount; ++j, ++k1, ++k2) {
            if (i != 0) {
                indices.push_back(k1);
                indices.push_back(k2);
                indices.push_back(k1 + 1);
            }

            if (i != (stackCount - 1)) {
                indices.push_back(k1 + 1);
                indices.push_back(k2);
                indices.push_back(k2 + 1);
            }
        }
    }

    return indices;
}

// =============================================================================
// PBR Demo Application
// =============================================================================

class PBRDemo {
public:
    PBRDemo() {
        initWindow();
        initRHI();
        initResources();
        initPipeline();
    }

    ~PBRDemo() {
        if (m_device) {
            m_device->waitIdle();
        }

        m_pipeline.reset();
        m_bindGroups.clear();
        m_uniformBuffers.clear();
        m_bindGroupLayout.reset();
        m_pipelineLayout.reset();
        m_vertexBuffer.reset();
        m_indexBuffer.reset();
        m_mirrorVertexBuffer.reset();
        m_mirrorIndexBuffer.reset();
        m_sampler.reset();
        m_depthView.reset();
        m_depthTexture.reset();
        m_skyboxRenderer.reset();
        m_iblManager.reset();
        m_resourceManager.reset();
        m_bridge.reset();

        if (m_window) {
            glfwDestroyWindow(m_window);
            glfwTerminate();
        }
    }

    void run() {
        while (!glfwWindowShouldClose(m_window)) {
            glfwPollEvents();
            render();
        }
        m_device->waitIdle();
    }

private:
    // Scene constants
    static constexpr uint32_t GRID_SIZE = 7;
    static constexpr uint32_t GRID_COUNT = GRID_SIZE * GRID_SIZE;  // 49
    static constexpr uint32_t MIRROR_INDEX = GRID_COUNT;           // index 49
    static constexpr uint32_t TOTAL_OBJECTS = GRID_COUNT + 1;      // 50
    static constexpr float SPHERE_SPACING = 2.5f;
    static constexpr float MIRROR_RADIUS = 2.0f;

    // Window
    GLFWwindow* m_window = nullptr;
    static constexpr uint32_t WIDTH = 1280;
    static constexpr uint32_t HEIGHT = 720;

    // Camera
    Camera m_camera{static_cast<float>(WIDTH) / static_cast<float>(HEIGHT)};
    bool m_mousePressed = false;
    double m_lastX = 0.0;
    double m_lastY = 0.0;

    // RHI
    std::unique_ptr<rendering::RendererBridge> m_bridge;
    rhi::RHIDevice* m_device = nullptr;
    rhi::RHIQueue* m_queue = nullptr;
    rhi::RHISwapchain* m_swapchain = nullptr;
    void* m_nativeRenderPass = nullptr;

    std::unique_ptr<rhi::RHITexture> m_depthTexture;
    std::unique_ptr<rhi::RHITextureView> m_depthView;
    std::unique_ptr<ResourceManager> m_resourceManager;
    std::unique_ptr<rendering::IBLManager> m_iblManager;
    std::unique_ptr<rendering::SkyboxRenderer> m_skyboxRenderer;
    uint32_t m_frameIndex = 0;

    // Rendering
    std::unique_ptr<rhi::RHIRenderPipeline> m_pipeline;
    std::unique_ptr<rhi::RHIBindGroupLayout> m_bindGroupLayout;
    std::unique_ptr<rhi::RHIPipelineLayout> m_pipelineLayout;

    // Grid sphere mesh (radius 1.0)
    std::unique_ptr<rhi::RHIBuffer> m_vertexBuffer;
    std::unique_ptr<rhi::RHIBuffer> m_indexBuffer;
    uint32_t m_indexCount = 0;

    // Mirror sphere mesh (radius 2.0)
    std::unique_ptr<rhi::RHIBuffer> m_mirrorVertexBuffer;
    std::unique_ptr<rhi::RHIBuffer> m_mirrorIndexBuffer;
    uint32_t m_mirrorIndexCount = 0;

    std::unique_ptr<rhi::RHISampler> m_sampler;

    // Per-object resources (50 objects)
    std::vector<std::unique_ptr<rhi::RHIBuffer>> m_uniformBuffers;
    std::vector<std::unique_ptr<rhi::RHIBindGroup>> m_bindGroups;

    // Uniform structure (must match shader)
    struct UniformData {
        alignas(16) glm::mat4 model;
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 proj;
        alignas(16) glm::vec3 sunDirection{0.5f, -0.8f, 0.3f};
        alignas(4) float sunIntensity = 2.0f;
        alignas(16) glm::vec3 sunColor{1.0f, 0.98f, 0.95f};
        alignas(4) float ambientIntensity = 1.0f;
        alignas(16) glm::vec3 cameraPos;
        alignas(4) float exposure = 1.2f;
        alignas(16) glm::vec3 albedo{0.95f, 0.79f, 0.25f};
        alignas(4) float metallic = 0.0f;
        alignas(4) float roughness = 0.5f;
        alignas(4) float ao = 1.0f;
    };

    UniformData m_uniformData{};

    // =========================================================================
    // Initialization
    // =========================================================================

    void initWindow() {
        if (!glfwInit()) {
            throw std::runtime_error("Failed to initialize GLFW");
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        m_window = glfwCreateWindow(WIDTH, HEIGHT, "PBR/IBL Material Showcase", nullptr, nullptr);
        glfwSetWindowUserPointer(m_window, this);

        glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow* w, int width, int height) {
            auto* app = static_cast<PBRDemo*>(glfwGetWindowUserPointer(w));
            app->onResize(width, height);
        });

        glfwSetCursorPosCallback(m_window, [](GLFWwindow* w, double x, double y) {
            auto* app = static_cast<PBRDemo*>(glfwGetWindowUserPointer(w));
            app->onMouseMove(x, y);
        });

        glfwSetMouseButtonCallback(m_window, [](GLFWwindow* w, int button, int action, int /*mods*/) {
            auto* app = static_cast<PBRDemo*>(glfwGetWindowUserPointer(w));
            if (button == GLFW_MOUSE_BUTTON_LEFT) {
                app->m_mousePressed = (action == GLFW_PRESS);
                if (app->m_mousePressed) {
                    glfwGetCursorPos(w, &app->m_lastX, &app->m_lastY);
                }
            }
        });

        glfwSetScrollCallback(m_window, [](GLFWwindow* w, double /*xoffset*/, double yoffset) {
            auto* app = static_cast<PBRDemo*>(glfwGetWindowUserPointer(w));
            app->m_camera.zoom(static_cast<float>(yoffset) * 0.5f);
        });
    }

    void initRHI() {
        m_bridge = std::make_unique<rendering::RendererBridge>(m_window);
        m_device = m_bridge->getDevice();
        m_queue = m_bridge->getGraphicsQueue();

        m_bridge->createSwapchain(WIDTH, HEIGHT, true);
        m_swapchain = m_bridge->getSwapchain();

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
        auto* vulkanSwapchain = static_cast<RHI::Vulkan::VulkanRHISwapchain*>(m_swapchain);
        VkRenderPass vkRenderPass = static_cast<VkRenderPass>(vulkanSwapchain->getRenderPass());
        m_nativeRenderPass = reinterpret_cast<void*>(vkRenderPass);
#endif

        createDepthResources();
        m_swapchain->ensureRenderResourcesReady(m_depthView.get());

        m_resourceManager = std::make_unique<ResourceManager>(m_device, m_queue);
        m_iblManager = std::make_unique<rendering::IBLManager>(m_device, m_queue);

        // Load HDR environment — studio for clear reflections
        bool iblInitialized = false;
        try {
            rhi::RHITexture* hdrTexture = m_resourceManager->loadHDRTexture("textures/ferndale_studio_12_4k.hdr");
            if (hdrTexture && m_iblManager->initialize(hdrTexture)) {
                std::cout << "[PBR] IBL initialized with ferndale_studio HDR" << std::endl;
                iblInitialized = true;
            }
        } catch (const std::exception& e) {
            std::cout << "[PBR] Could not load HDR: " << e.what() << std::endl;
        }

        if (!iblInitialized) {
            if (!m_iblManager->initializeDefault()) {
                throw std::runtime_error("Failed to initialize IBL");
            }
        }

        // Skybox
        m_skyboxRenderer = std::make_unique<rendering::SkyboxRenderer>(m_device, m_queue);
        if (!m_skyboxRenderer->initialize(m_swapchain->getFormat(),
                                           rhi::TextureFormat::Depth32Float,
                                           m_nativeRenderPass)) {
            throw std::runtime_error("Failed to initialize SkyboxRenderer");
        }
        m_skyboxRenderer->setSunDirection(glm::normalize(m_uniformData.sunDirection));

        if (m_iblManager->isInitialized() && m_iblManager->getEnvironmentMap()) {
            m_skyboxRenderer->setEnvironmentMap(m_iblManager->getEnvironmentView(),
                                                 m_iblManager->getSampler());
            m_skyboxRenderer->setExposure(1.0f);
        }

        // Camera: look at grid center, slightly elevated angle
        float gridCenter = (GRID_SIZE - 1) * SPHERE_SPACING * 0.5f;
        m_camera.setTarget(glm::vec3(gridCenter * 0.5f, gridCenter * 0.5f, 0.0f));
        m_camera.setDistance(22.0f);
    }

    void createDepthResources() {
        rhi::TextureDesc depthDesc{};
        depthDesc.size = {m_swapchain->getWidth(), m_swapchain->getHeight(), 1};
        depthDesc.format = rhi::TextureFormat::Depth32Float;
        depthDesc.usage = rhi::TextureUsage::DepthStencil;

        m_depthTexture = m_device->createTexture(depthDesc);
        m_depthView = m_depthTexture->createDefaultView();
    }

    void initResources() {
        // Grid sphere mesh (radius 1.0, high detail)
        {
            const auto vertices = generateSphereVertices(1.0f, 64, 32);
            const auto indices = generateSphereIndices(64, 32);
            m_indexCount = static_cast<uint32_t>(indices.size());

            rhi::BufferDesc vbDesc{};
            vbDesc.size = vertices.size() * sizeof(SphereVertex);
            vbDesc.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::CopyDst;
            m_vertexBuffer = m_device->createBuffer(vbDesc);
            m_vertexBuffer->write(vertices.data(), vbDesc.size, 0);

            rhi::BufferDesc ibDesc{};
            ibDesc.size = indices.size() * sizeof(uint32_t);
            ibDesc.usage = rhi::BufferUsage::Index | rhi::BufferUsage::CopyDst;
            m_indexBuffer = m_device->createBuffer(ibDesc);
            m_indexBuffer->write(indices.data(), ibDesc.size, 0);
        }

        // Mirror sphere mesh (radius 2.0, higher detail)
        {
            const auto vertices = generateSphereVertices(MIRROR_RADIUS, 96, 48);
            const auto indices = generateSphereIndices(96, 48);
            m_mirrorIndexCount = static_cast<uint32_t>(indices.size());

            rhi::BufferDesc vbDesc{};
            vbDesc.size = vertices.size() * sizeof(SphereVertex);
            vbDesc.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::CopyDst;
            m_mirrorVertexBuffer = m_device->createBuffer(vbDesc);
            m_mirrorVertexBuffer->write(vertices.data(), vbDesc.size, 0);

            rhi::BufferDesc ibDesc{};
            ibDesc.size = indices.size() * sizeof(uint32_t);
            ibDesc.usage = rhi::BufferUsage::Index | rhi::BufferUsage::CopyDst;
            m_mirrorIndexBuffer = m_device->createBuffer(ibDesc);
            m_mirrorIndexBuffer->write(indices.data(), ibDesc.size, 0);
        }

        // Per-object uniform buffers (50 objects)
        rhi::BufferDesc ubDesc{};
        ubDesc.size = sizeof(UniformData);
        ubDesc.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDst;

        m_uniformBuffers.resize(TOTAL_OBJECTS);
        for (uint32_t i = 0; i < TOTAL_OBJECTS; ++i) {
            m_uniformBuffers[i] = m_device->createBuffer(ubDesc);
        }

        // Sampler
        rhi::SamplerDesc samplerDesc{};
        samplerDesc.magFilter = rhi::FilterMode::Linear;
        samplerDesc.minFilter = rhi::FilterMode::Linear;
        samplerDesc.mipmapFilter = rhi::MipmapMode::Linear;
        samplerDesc.addressModeU = rhi::AddressMode::ClampToEdge;
        samplerDesc.addressModeV = rhi::AddressMode::ClampToEdge;
        samplerDesc.addressModeW = rhi::AddressMode::ClampToEdge;
        m_sampler = m_device->createSampler(samplerDesc);
    }

    void initPipeline() {
        auto vertShader = m_bridge->createShaderFromFile("shaders/pbr_sphere.vert.spv", rhi::ShaderStage::Vertex);
        auto fragShader = m_bridge->createShaderFromFile("shaders/pbr_sphere.frag.spv", rhi::ShaderStage::Fragment);
        std::cout << "[PBR] Shaders: vert=" << (vertShader ? "OK" : "FAIL")
                  << " frag=" << (fragShader ? "OK" : "FAIL") << "\n";

        // Bind group layout
        rhi::BindGroupLayoutEntry uniformEntry(0, rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, rhi::BindingType::UniformBuffer);

        rhi::BindGroupLayoutEntry irradianceEntry(1, rhi::ShaderStage::Fragment, rhi::BindingType::SampledTexture);
        irradianceEntry.textureViewDimension = rhi::TextureViewDimension::ViewCube;

        rhi::BindGroupLayoutEntry prefilteredEntry(2, rhi::ShaderStage::Fragment, rhi::BindingType::SampledTexture);
        prefilteredEntry.textureViewDimension = rhi::TextureViewDimension::ViewCube;

        rhi::BindGroupLayoutEntry brdfEntry(3, rhi::ShaderStage::Fragment, rhi::BindingType::SampledTexture);
        brdfEntry.textureViewDimension = rhi::TextureViewDimension::View2D;

        rhi::BindGroupLayoutEntry samplerEntry(4, rhi::ShaderStage::Fragment, rhi::BindingType::Sampler);

        rhi::BindGroupLayoutDesc bgLayoutDesc{};
        bgLayoutDesc.entries = {uniformEntry, irradianceEntry, prefilteredEntry, brdfEntry, samplerEntry};
        m_bindGroupLayout = m_device->createBindGroupLayout(bgLayoutDesc);

        // Pipeline layout
        rhi::PipelineLayoutDesc plDesc{};
        plDesc.bindGroupLayouts = {m_bindGroupLayout.get()};
        m_pipelineLayout = m_device->createPipelineLayout(plDesc);

        // Vertex layout
        std::vector<rhi::VertexAttribute> attributes = {
            {0, 0, rhi::TextureFormat::RGB32Float, 0},
            {1, 0, rhi::TextureFormat::RGB32Float, sizeof(glm::vec3)},
            {2, 0, rhi::TextureFormat::RG32Float, 2 * sizeof(glm::vec3)},
        };
        std::vector<rhi::VertexBufferLayout> bufferLayouts = {
            {sizeof(SphereVertex), attributes}
        };

        // Depth stencil
        rhi::DepthStencilState depthState{};
        depthState.format = rhi::TextureFormat::Depth32Float;
        depthState.depthWriteEnabled = true;
        depthState.depthCompare = rhi::CompareOp::Less;

        // Pipeline
        rhi::RenderPipelineDesc pipelineDesc{};
        pipelineDesc.vertexShader = vertShader.get();
        pipelineDesc.fragmentShader = fragShader.get();
        pipelineDesc.layout = m_pipelineLayout.get();
        pipelineDesc.vertex.buffers = bufferLayouts;
        pipelineDesc.colorTargets = {rhi::ColorTargetState(m_swapchain->getFormat())};
        pipelineDesc.depthStencil = &depthState;
        pipelineDesc.primitive.topology = rhi::PrimitiveTopology::TriangleList;
        pipelineDesc.primitive.cullMode = rhi::CullMode::Back;
        pipelineDesc.primitive.frontFace = rhi::FrontFace::Clockwise;
        pipelineDesc.nativeRenderPass = m_nativeRenderPass;

        m_pipeline = m_device->createRenderPipeline(pipelineDesc);
        std::cout << "[PBR] Pipeline: " << (m_pipeline ? "OK" : "FAIL") << "\n";

        // Create bind groups for all 50 objects
        m_bindGroups.resize(TOTAL_OBJECTS);
        for (uint32_t i = 0; i < TOTAL_OBJECTS; ++i) {
            rhi::BindGroupDesc bgDesc{};
            bgDesc.layout = m_bindGroupLayout.get();
            bgDesc.entries = {
                rhi::BindGroupEntry::Buffer(0, m_uniformBuffers[i].get(), 0, sizeof(UniformData)),
                rhi::BindGroupEntry::TextureView(1, m_iblManager->getIrradianceView()),
                rhi::BindGroupEntry::TextureView(2, m_iblManager->getPrefilteredView()),
                rhi::BindGroupEntry::TextureView(3, m_iblManager->getBRDFLutView()),
                rhi::BindGroupEntry::Sampler(4, m_sampler.get()),
            };
            m_bindGroups[i] = m_device->createBindGroup(bgDesc);
        }

        std::cout << "[PBR] Created " << TOTAL_OBJECTS << " bind groups ("
                  << GRID_SIZE << "x" << GRID_SIZE << " grid + 1 mirror)\n";
    }

    // =========================================================================
    // Rendering
    // =========================================================================

    void render() {
        updateUniforms();

        if (!m_bridge->beginFrame()) {
            return;
        }

        auto commandEncoder = m_bridge->createCommandEncoder();
        uint32_t width = m_swapchain->getWidth();
        uint32_t height = m_swapchain->getHeight();

        // Render pass setup
        rhi::RenderPassColorAttachment colorAttachment{};
        colorAttachment.view = m_bridge->getCurrentSwapchainView();
        colorAttachment.loadOp = rhi::LoadOp::Clear;
        colorAttachment.storeOp = rhi::StoreOp::Store;
        colorAttachment.clearValue = rhi::ClearColorValue(0.01f, 0.01f, 0.01f, 1.0f);

        rhi::RenderPassDepthStencilAttachment depthAttachment{};
        depthAttachment.view = m_depthView.get();
        depthAttachment.depthLoadOp = rhi::LoadOp::Clear;
        depthAttachment.depthStoreOp = rhi::StoreOp::Store;
        depthAttachment.depthClearValue = 1.0f;

        rhi::RenderPassDesc renderPassDesc{};
        renderPassDesc.colorAttachments = {colorAttachment};
        renderPassDesc.depthStencilAttachment = &depthAttachment;
        renderPassDesc.width = width;
        renderPassDesc.height = height;

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
        auto* vulkanSwapchain = static_cast<RHI::Vulkan::VulkanRHISwapchain*>(m_swapchain);
        uint32_t imageIndex = vulkanSwapchain->getCurrentImageIndex();
        VkFramebuffer vkFramebuffer = static_cast<VkFramebuffer>(vulkanSwapchain->getFramebuffer(imageIndex));
        renderPassDesc.nativeRenderPass = m_nativeRenderPass;
        renderPassDesc.nativeFramebuffer = reinterpret_cast<void*>(vkFramebuffer);
#endif

        auto renderPass = commandEncoder->beginRenderPass(renderPassDesc);

        // Skybox
        {
            glm::mat4 view = m_camera.getViewMatrix();
            glm::mat4 proj = m_camera.getProjectionMatrix();
            glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(view));
            glm::mat4 invViewProj = glm::inverse(proj * viewNoTranslation);
            float time = static_cast<float>(glfwGetTime());

            renderPass->setViewport(0, 0, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f);
            renderPass->setScissorRect(0, 0, width, height);
            m_skyboxRenderer->render(renderPass.get(), m_frameIndex, invViewProj, time);
        }

        // PBR objects
        renderPass->setPipeline(m_pipeline.get());
        renderPass->setViewport(0, 0, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f);
        renderPass->setScissorRect(0, 0, width, height);

        // Draw 7x7 grid spheres
        renderPass->setVertexBuffer(0, m_vertexBuffer.get(), 0);
        renderPass->setIndexBuffer(m_indexBuffer.get(), rhi::IndexFormat::Uint32, 0);

        for (uint32_t i = 0; i < GRID_COUNT; ++i) {
            renderPass->setBindGroup(0, m_bindGroups[i].get());
            renderPass->drawIndexed(m_indexCount, 1, 0, 0, 0);
        }

        // Draw mirror sphere (different mesh)
        renderPass->setVertexBuffer(0, m_mirrorVertexBuffer.get(), 0);
        renderPass->setIndexBuffer(m_mirrorIndexBuffer.get(), rhi::IndexFormat::Uint32, 0);
        renderPass->setBindGroup(0, m_bindGroups[MIRROR_INDEX].get());
        renderPass->drawIndexed(m_mirrorIndexCount, 1, 0, 0, 0);

        renderPass->end();

        auto commandBuffer = commandEncoder->finish();
        m_bridge->submitCommandBuffer(
            commandBuffer.get(),
            m_bridge->getImageAvailableSemaphore(),
            m_bridge->getRenderFinishedSemaphore(),
            m_bridge->getInFlightFence()
        );
        m_bridge->endFrame();
        m_frameIndex++;
    }

    void updateUniforms() {
        m_uniformData.view = m_camera.getViewMatrix();
        m_uniformData.proj = m_camera.getProjectionMatrix();
        m_uniformData.cameraPos = m_camera.getPosition();

        // Grid center offset (to center grid around origin)
        float gridExtent = (GRID_SIZE - 1) * SPHERE_SPACING;
        float offsetX = -gridExtent * 0.5f;
        float offsetY = -gridExtent * 0.5f;

        // 7x7 grid: X = roughness (0→1), Y = metallic (0→1)
        for (uint32_t row = 0; row < GRID_SIZE; ++row) {
            for (uint32_t col = 0; col < GRID_SIZE; ++col) {
                uint32_t idx = row * GRID_SIZE + col;

                float x = offsetX + col * SPHERE_SPACING;
                float y = offsetY + row * SPHERE_SPACING;

                m_uniformData.model = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, 0.0f));
                m_uniformData.albedo = glm::vec3(0.95f, 0.79f, 0.25f);  // Gold
                m_uniformData.roughness = static_cast<float>(col) / (GRID_SIZE - 1);
                m_uniformData.metallic = static_cast<float>(row) / (GRID_SIZE - 1);
                // Clamp roughness minimum to avoid singularities
                m_uniformData.roughness = glm::max(m_uniformData.roughness, 0.05f);

                m_uniformBuffers[idx]->write(&m_uniformData, sizeof(UniformData), 0);
            }
        }

        // Mirror sphere: placed to the right of the grid
        {
            float mirrorX = gridExtent * 0.5f + SPHERE_SPACING * 2.0f + MIRROR_RADIUS;

            m_uniformData.model = glm::translate(glm::mat4(1.0f), glm::vec3(mirrorX, 0.0f, 0.0f));
            m_uniformData.albedo = glm::vec3(0.98f, 0.98f, 0.98f);  // Near-white for bright reflections
            m_uniformData.metallic = 1.0f;
            m_uniformData.roughness = 0.02f;  // Near-perfect mirror
            m_uniformBuffers[MIRROR_INDEX]->write(&m_uniformData, sizeof(UniformData), 0);
        }
    }

    // =========================================================================
    // Input
    // =========================================================================

    void onMouseMove(double x, double y) {
        if (!m_mousePressed) {
            m_lastX = x;
            m_lastY = y;
            return;
        }

        float dx = static_cast<float>(x - m_lastX);
        float dy = static_cast<float>(y - m_lastY);
        m_lastX = x;
        m_lastY = y;

        m_camera.rotate(dx * 0.5f, dy * 0.5f);
    }

    void onResize(int width, int height) {
        if (width == 0 || height == 0) return;
        m_device->waitIdle();
        m_bridge->onResize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        m_swapchain = m_bridge->getSwapchain();
        m_camera.setAspectRatio(static_cast<float>(width) / static_cast<float>(height));
        createDepthResources();
    }
};

// =============================================================================
// Main
// =============================================================================

int main() {
    try {
        PBRDemo demo;
        demo.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
