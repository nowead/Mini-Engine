/**
 * @file dual_light_demo.cpp
 * @brief Dual Point Light PBR Demo - Metallic vs Dielectric comparison
 *
 * Demonstrates:
 * - Cook-Torrance PBR with 2 colored point lights (blue left, red right)
 * - Metallic sphere vs Dielectric sphere on infinite floor
 * - No IBL, no skybox (pure black background)
 * - Direct lighting attenuation and color mixing
 * - Orbit camera controls (left-drag rotate, scroll zoom)
 */

#include <rhi/RHI.hpp>
#include "src/rendering/RendererBridge.hpp"
#include "src/scene/Camera.hpp"
#include "src/utils/Vertex.hpp"

// Platform-specific Vulkan includes for traditional render pass (Linux)
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
        const float xy = radius * std::cos(stackAngle);
        const float z = radius * std::sin(stackAngle);

        for (uint32_t j = 0; j <= sectorCount; ++j) {
            const float sectorAngle = j * sectorStep;
            SphereVertex vertex{};
            vertex.position.x = xy * std::cos(sectorAngle);
            vertex.position.y = xy * std::sin(sectorAngle);
            vertex.position.z = z;
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
// Plane Mesh Generation
// =============================================================================

std::vector<SphereVertex> generatePlaneVertices(float width, float depth, uint32_t segmentsX, uint32_t segmentsZ) {
    std::vector<SphereVertex> vertices;

    for (uint32_t z = 0; z <= segmentsZ; ++z) {
        for (uint32_t x = 0; x <= segmentsX; ++x) {
            float xPos = (x / (float)segmentsX - 0.5f) * width;
            float zPos = (z / (float)segmentsZ - 0.5f) * depth;

            SphereVertex vertex{};
            vertex.position = glm::vec3(xPos, 0.0f, zPos);
            vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);  // Up
            vertex.texCoord = glm::vec2(x / (float)segmentsX, z / (float)segmentsZ);
            vertices.push_back(vertex);
        }
    }

    return vertices;
}

std::vector<uint32_t> generatePlaneIndices(uint32_t segmentsX, uint32_t segmentsZ) {
    std::vector<uint32_t> indices;

    for (uint32_t z = 0; z < segmentsZ; ++z) {
        for (uint32_t x = 0; x < segmentsX; ++x) {
            uint32_t topLeft = z * (segmentsX + 1) + x;
            uint32_t topRight = topLeft + 1;
            uint32_t bottomLeft = (z + 1) * (segmentsX + 1) + x;
            uint32_t bottomRight = bottomLeft + 1;

            // Triangle 1
            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);

            // Triangle 2
            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }

    return indices;
}

// =============================================================================
// Dual Light Demo Application
// =============================================================================

class DualLightDemo {
public:
    DualLightDemo() {
        initWindow();
        initRHI();
        initResources();
        initPipeline();
    }

    ~DualLightDemo() {
        if (m_device) {
            m_device->waitIdle();
        }

        // Release GPU resources before bridge destroys device
        m_pipeline.reset();
        m_floorBindGroup.reset();
        m_metallicSphereBindGroup.reset();
        m_dielectricSphereBindGroup.reset();
        m_bindGroupLayout.reset();
        m_pipelineLayout.reset();
        m_sphereVertexBuffer.reset();
        m_sphereIndexBuffer.reset();
        m_floorVertexBuffer.reset();
        m_floorIndexBuffer.reset();
        m_floorUniformBuffer.reset();
        m_metallicSphereUniformBuffer.reset();
        m_dielectricSphereUniformBuffer.reset();
        m_depthView.reset();
        m_depthTexture.reset();

        // Bridge destructor will clean up device, swapchain, etc.
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
    // Window
    GLFWwindow* m_window = nullptr;
    static constexpr uint32_t WIDTH = 1280;
    static constexpr uint32_t HEIGHT = 720;

    // Camera (orbit style)
    Camera m_camera{static_cast<float>(WIDTH) / static_cast<float>(HEIGHT)};
    bool m_mousePressed = false;
    double m_lastX = 0.0;
    double m_lastY = 0.0;

    // RHI Resources (non-owning pointers from RendererBridge)
    std::unique_ptr<rendering::RendererBridge> m_bridge;
    rhi::RHIDevice* m_device = nullptr;
    rhi::RHIQueue* m_queue = nullptr;
    rhi::RHISwapchain* m_swapchain = nullptr;
    void* m_nativeRenderPass = nullptr;

    std::unique_ptr<rhi::RHITexture> m_depthTexture;
    std::unique_ptr<rhi::RHITextureView> m_depthView;
    uint32_t m_frameIndex = 0;

    // Rendering Resources
    std::unique_ptr<rhi::RHIRenderPipeline> m_pipeline;
    std::unique_ptr<rhi::RHIBindGroupLayout> m_bindGroupLayout;
    std::unique_ptr<rhi::RHIPipelineLayout> m_pipelineLayout;

    // Mesh buffers
    std::unique_ptr<rhi::RHIBuffer> m_sphereVertexBuffer;
    std::unique_ptr<rhi::RHIBuffer> m_sphereIndexBuffer;
    uint32_t m_sphereIndexCount = 0;

    std::unique_ptr<rhi::RHIBuffer> m_floorVertexBuffer;
    std::unique_ptr<rhi::RHIBuffer> m_floorIndexBuffer;
    uint32_t m_floorIndexCount = 0;

    // Per-object uniform buffers and bind groups
    std::unique_ptr<rhi::RHIBuffer> m_floorUniformBuffer;
    std::unique_ptr<rhi::RHIBindGroup> m_floorBindGroup;
    std::unique_ptr<rhi::RHIBuffer> m_metallicSphereUniformBuffer;
    std::unique_ptr<rhi::RHIBindGroup> m_metallicSphereBindGroup;
    std::unique_ptr<rhi::RHIBuffer> m_dielectricSphereUniformBuffer;
    std::unique_ptr<rhi::RHIBindGroup> m_dielectricSphereBindGroup;

    // Uniform Data Structure
    struct UniformData {
        alignas(16) glm::mat4 model;
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 proj;

        // Blue point light (left)
        alignas(16) glm::vec3 light1Position{-5.0f, 3.0f, 0.0f};
        alignas(4)  float light1Intensity = 100.0f;  // Increased for better visibility
        alignas(16) glm::vec3 light1Color{0.0f, 0.0f, 1.0f};  // Pure Blue
        alignas(4)  float light1Radius = 15.0f;  // Increased radius for softer falloff

        // Red point light (right)
        alignas(16) glm::vec3 light2Position{5.0f, 3.0f, 0.0f};
        alignas(4)  float light2Intensity = 100.0f;  // Increased for better visibility
        alignas(16) glm::vec3 light2Color{1.0f, 0.0f, 0.0f};  // Pure Red
        alignas(4)  float light2Radius = 15.0f;  // Increased radius for softer falloff

        alignas(16) glm::vec3 cameraPos;
        alignas(4)  float exposure = 1.0f;

        alignas(16) glm::vec3 albedo{1.0f, 1.0f, 1.0f};
        alignas(4)  float metallic = 0.0f;
        alignas(4)  float roughness = 0.5f;
        alignas(4)  float ao = 1.0f;

        alignas(16) glm::vec3 ambientColor{1.0f, 1.0f, 1.0f};
        alignas(4)  float ambientIntensity = 0.0f;  // No ambient - pure point light response only
    };

    UniformData m_uniformData{};

    void initWindow() {
        if (!glfwInit()) {
            throw std::runtime_error("Failed to initialize GLFW");
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        m_window = glfwCreateWindow(WIDTH, HEIGHT, "Dual Light PBR Demo", nullptr, nullptr);
        glfwSetWindowUserPointer(m_window, this);

        // Callbacks
        glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow* w, int width, int height) {
            auto* app = static_cast<DualLightDemo*>(glfwGetWindowUserPointer(w));
            app->onResize(width, height);
        });

        glfwSetCursorPosCallback(m_window, [](GLFWwindow* w, double x, double y) {
            auto* app = static_cast<DualLightDemo*>(glfwGetWindowUserPointer(w));
            app->onMouseMove(x, y);
        });

        glfwSetMouseButtonCallback(m_window, [](GLFWwindow* w, int button, int action, int /*mods*/) {
            auto* app = static_cast<DualLightDemo*>(glfwGetWindowUserPointer(w));
            if (button == GLFW_MOUSE_BUTTON_LEFT) {
                app->m_mousePressed = (action == GLFW_PRESS);
                if (app->m_mousePressed) {
                    glfwGetCursorPos(w, &app->m_lastX, &app->m_lastY);
                }
            }
        });

        glfwSetScrollCallback(m_window, [](GLFWwindow* w, double /*xoffset*/, double yoffset) {
            auto* app = static_cast<DualLightDemo*>(glfwGetWindowUserPointer(w));
            app->m_camera.zoom(static_cast<float>(yoffset) * 0.5f);
        });
    }

    void initRHI() {
        m_bridge = std::make_unique<rendering::RendererBridge>(m_window);
        m_device = m_bridge->getDevice();
        m_queue = m_bridge->getGraphicsQueue();

        // Create swapchain explicitly
        m_bridge->createSwapchain(WIDTH, HEIGHT, true);
        m_swapchain = m_bridge->getSwapchain();

        // Get native render pass for Linux traditional rendering
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
        auto* vulkanSwapchain = static_cast<RHI::Vulkan::VulkanRHISwapchain*>(m_swapchain);
        VkRenderPass vkRenderPass = static_cast<VkRenderPass>(vulkanSwapchain->getRenderPass());
        m_nativeRenderPass = reinterpret_cast<void*>(vkRenderPass);
#endif

        createDepthResources();

        // Ensure framebuffers are created with depth view
        m_swapchain->ensureRenderResourcesReady(m_depthView.get());

        // Position camera for the scene
        m_camera.setTarget(glm::vec3(0.0f, 0.5f, 0.0f));
        m_camera.setDistance(10.0f);
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
        // Generate sphere mesh
        const auto sphereVertices = generateSphereVertices(1.0f, 64, 32);
        const auto sphereIndices = generateSphereIndices(64, 32);
        m_sphereIndexCount = static_cast<uint32_t>(sphereIndices.size());

        // Create sphere vertex buffer
        rhi::BufferDesc svbDesc{};
        svbDesc.size = sphereVertices.size() * sizeof(SphereVertex);
        svbDesc.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::CopyDst;
        m_sphereVertexBuffer = m_device->createBuffer(svbDesc);
        m_sphereVertexBuffer->write(sphereVertices.data(), svbDesc.size, 0);

        // Create sphere index buffer
        rhi::BufferDesc sibDesc{};
        sibDesc.size = sphereIndices.size() * sizeof(uint32_t);
        sibDesc.usage = rhi::BufferUsage::Index | rhi::BufferUsage::CopyDst;
        m_sphereIndexBuffer = m_device->createBuffer(sibDesc);
        m_sphereIndexBuffer->write(sphereIndices.data(), sibDesc.size, 0);

        // Generate floor mesh (100x100, 10 segments each axis)
        const auto floorVertices = generatePlaneVertices(100.0f, 100.0f, 10, 10);
        const auto floorIndices = generatePlaneIndices(10, 10);
        m_floorIndexCount = static_cast<uint32_t>(floorIndices.size());

        // Create floor vertex buffer
        rhi::BufferDesc fvbDesc{};
        fvbDesc.size = floorVertices.size() * sizeof(SphereVertex);
        fvbDesc.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::CopyDst;
        m_floorVertexBuffer = m_device->createBuffer(fvbDesc);
        m_floorVertexBuffer->write(floorVertices.data(), fvbDesc.size, 0);

        // Create floor index buffer
        rhi::BufferDesc fibDesc{};
        fibDesc.size = floorIndices.size() * sizeof(uint32_t);
        fibDesc.usage = rhi::BufferUsage::Index | rhi::BufferUsage::CopyDst;
        m_floorIndexBuffer = m_device->createBuffer(fibDesc);
        m_floorIndexBuffer->write(floorIndices.data(), fibDesc.size, 0);

        // Create uniform buffers for each object
        rhi::BufferDesc ubDesc{};
        ubDesc.size = sizeof(UniformData);
        ubDesc.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDst;

        m_floorUniformBuffer = m_device->createBuffer(ubDesc);
        m_metallicSphereUniformBuffer = m_device->createBuffer(ubDesc);
        m_dielectricSphereUniformBuffer = m_device->createBuffer(ubDesc);
    }

    void initPipeline() {
        // Load shaders via RendererBridge
        auto vertShader = m_bridge->createShaderFromFile("shaders/dual_light_pbr.vert.spv", rhi::ShaderStage::Vertex);
        auto fragShader = m_bridge->createShaderFromFile("shaders/dual_light_pbr.frag.spv", rhi::ShaderStage::Fragment);
        std::cout << "[DualLight] Shaders loaded: vert=" << (vertShader ? "OK" : "FAIL")
                  << " frag=" << (fragShader ? "OK" : "FAIL") << "\n";

        // Bind group layout (ONLY uniform buffer, no IBL textures)
        rhi::BindGroupLayoutEntry uniformEntry(0, rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, rhi::BindingType::UniformBuffer);

        rhi::BindGroupLayoutDesc bgLayoutDesc{};
        bgLayoutDesc.entries = {uniformEntry};
        m_bindGroupLayout = m_device->createBindGroupLayout(bgLayoutDesc);

        // Pipeline layout
        rhi::PipelineLayoutDesc plDesc{};
        plDesc.bindGroupLayouts = {m_bindGroupLayout.get()};
        m_pipelineLayout = m_device->createPipelineLayout(plDesc);

        // Vertex layout
        std::vector<rhi::VertexAttribute> attributes = {
            {0, 0, rhi::TextureFormat::RGB32Float, 0},                       // position
            {1, 0, rhi::TextureFormat::RGB32Float, sizeof(glm::vec3)},       // normal
            {2, 0, rhi::TextureFormat::RG32Float, 2 * sizeof(glm::vec3)},   // texCoord
        };
        std::vector<rhi::VertexBufferLayout> bufferLayouts = {
            {sizeof(SphereVertex), attributes}
        };

        // Depth stencil state
        rhi::DepthStencilState depthState{};
        depthState.format = rhi::TextureFormat::Depth32Float;
        depthState.depthWriteEnabled = true;
        depthState.depthCompare = rhi::CompareOp::Less;

        // Pipeline descriptor
        rhi::RenderPipelineDesc pipelineDesc{};
        pipelineDesc.vertexShader = vertShader.get();
        pipelineDesc.fragmentShader = fragShader.get();
        pipelineDesc.layout = m_pipelineLayout.get();
        pipelineDesc.vertex.buffers = bufferLayouts;
        pipelineDesc.colorTargets = {rhi::ColorTargetState(m_swapchain->getFormat())};
        pipelineDesc.depthStencil = &depthState;
        pipelineDesc.primitive.topology = rhi::PrimitiveTopology::TriangleList;
        pipelineDesc.primitive.cullMode = rhi::CullMode::Back;
        pipelineDesc.nativeRenderPass = m_nativeRenderPass;

        m_pipeline = m_device->createRenderPipeline(pipelineDesc);
        std::cout << "[DualLight] Pipeline: " << (m_pipeline ? "OK" : "FAIL") << "\n";

        // Create bind groups for each object
        rhi::BindGroupDesc bgDesc{};
        bgDesc.layout = m_bindGroupLayout.get();

        // Floor bind group
        bgDesc.entries = {
            rhi::BindGroupEntry::Buffer(0, m_floorUniformBuffer.get(), 0, sizeof(UniformData)),
        };
        m_floorBindGroup = m_device->createBindGroup(bgDesc);

        // Metallic sphere bind group
        bgDesc.entries = {
            rhi::BindGroupEntry::Buffer(0, m_metallicSphereUniformBuffer.get(), 0, sizeof(UniformData)),
        };
        m_metallicSphereBindGroup = m_device->createBindGroup(bgDesc);

        // Dielectric sphere bind group
        bgDesc.entries = {
            rhi::BindGroupEntry::Buffer(0, m_dielectricSphereUniformBuffer.get(), 0, sizeof(UniformData)),
        };
        m_dielectricSphereBindGroup = m_device->createBindGroup(bgDesc);
    }

    void render() {
        updateUniforms();

        // Use RendererBridge frame lifecycle for proper synchronization
        if (!m_bridge->beginFrame()) {
            return; // Swapchain needs resize or not ready
        }

        auto commandEncoder = m_bridge->createCommandEncoder();
        uint32_t width = m_swapchain->getWidth();
        uint32_t height = m_swapchain->getHeight();

        // Begin render pass (clear to BLACK background)
        rhi::RenderPassColorAttachment colorAttachment{};
        colorAttachment.view = m_bridge->getCurrentSwapchainView();
        colorAttachment.loadOp = rhi::LoadOp::Clear;
        colorAttachment.storeOp = rhi::StoreOp::Store;
        colorAttachment.clearValue = rhi::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);  // Black

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
        // Linux Vulkan: Provide native render pass and framebuffer
        auto* vulkanSwapchain = static_cast<RHI::Vulkan::VulkanRHISwapchain*>(m_swapchain);
        uint32_t imageIndex = vulkanSwapchain->getCurrentImageIndex();
        VkFramebuffer vkFramebuffer = static_cast<VkFramebuffer>(vulkanSwapchain->getFramebuffer(imageIndex));
        renderPassDesc.nativeRenderPass = m_nativeRenderPass;
        renderPassDesc.nativeFramebuffer = reinterpret_cast<void*>(vkFramebuffer);
#endif

        auto renderPass = commandEncoder->beginRenderPass(renderPassDesc);

        // NO SKYBOX - pure black background

        // Set pipeline and viewport
        renderPass->setPipeline(m_pipeline.get());
        renderPass->setViewport(0, 0, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f);
        renderPass->setScissorRect(0, 0, width, height);

        // Draw floor (dark gray, dielectric)
        {
            m_uniformData.model = glm::mat4(1.0f);  // Identity (y=0)
            m_uniformData.albedo = glm::vec3(0.2f, 0.2f, 0.2f);
            m_uniformData.metallic = 0.0f;
            m_uniformData.roughness = 0.8f;
            m_floorUniformBuffer->write(&m_uniformData, sizeof(UniformData), 0);

            renderPass->setBindGroup(0, m_floorBindGroup.get());
            renderPass->setVertexBuffer(0, m_floorVertexBuffer.get(), 0);
            renderPass->setIndexBuffer(m_floorIndexBuffer.get(), rhi::IndexFormat::Uint32, 0);
            renderPass->drawIndexed(m_floorIndexCount, 1, 0, 0, 0);
        }

        // Switch to sphere mesh
        renderPass->setVertexBuffer(0, m_sphereVertexBuffer.get(), 0);
        renderPass->setIndexBuffer(m_sphereIndexBuffer.get(), rhi::IndexFormat::Uint32, 0);

        // Draw metallic sphere (left, x = -3.0)
        {
            m_uniformData.model = glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, 1.0f, 0.0f));
            m_uniformData.albedo = glm::vec3(0.9f, 0.9f, 0.9f);
            m_uniformData.metallic = 1.0f;  // METALLIC
            m_uniformData.roughness = 0.05f;  // Nearly perfect mirror surface
            m_metallicSphereUniformBuffer->write(&m_uniformData, sizeof(UniformData), 0);

            renderPass->setBindGroup(0, m_metallicSphereBindGroup.get());
            renderPass->drawIndexed(m_sphereIndexCount, 1, 0, 0, 0);
        }

        // Draw dielectric sphere (right, x = 3.0)
        {
            m_uniformData.model = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 1.0f, 0.0f));
            m_uniformData.albedo = glm::vec3(0.8f, 0.8f, 0.8f);
            m_uniformData.metallic = 0.0f;  // DIELECTRIC
            m_uniformData.roughness = 0.3f;
            m_dielectricSphereUniformBuffer->write(&m_uniformData, sizeof(UniformData), 0);

            renderPass->setBindGroup(0, m_dielectricSphereBindGroup.get());
            renderPass->drawIndexed(m_sphereIndexCount, 1, 0, 0, 0);
        }

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
    }

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
// Main Entry Point
// =============================================================================

int main() {
    try {
        DualLightDemo demo;
        demo.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
