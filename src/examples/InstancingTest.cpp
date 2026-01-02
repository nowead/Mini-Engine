#include "InstancingTest.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <cmath>
#include <fstream>
#include <vector>

namespace examples {

// Helper function to load SPIR-V from file
static std::vector<uint8_t> loadSPIRV(const char* filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error(std::string("Failed to open shader file: ") + filename);
    }

    size_t fileSize = file.tellg();
    std::vector<uint8_t> buffer(fileSize);

    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    file.close();

    return buffer;
}

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
};

struct InstanceData {
    glm::vec3 position;
    glm::vec3 color;
    float scale;
    float _padding; // Align to 16 bytes
};

struct CameraUBO {
    glm::mat4 view;
    glm::mat4 proj;
};

InstancingTest::InstancingTest(rhi::RHIDevice* device, int width, int height, void* nativeRenderPass)
    : m_device(device), m_nativeRenderPass(nativeRenderPass), m_width(width), m_height(height) {

    std::cout << "[InstancingTest] Initializing with " << width << "x" << height << std::endl;
}

InstancingTest::~InstancingTest() {
    std::cout << "[InstancingTest] Cleanup" << std::endl;
}

void InstancingTest::init() {
    std::cout << "[InstancingTest] Creating resources..." << std::endl;

    createCubeGeometry();
    createInstanceData();
    createUniformBuffer();
    createPipeline();

    std::cout << "[InstancingTest] Initialization complete! Ready to render "
              << INSTANCE_COUNT << " cubes." << std::endl;
}

void InstancingTest::createCubeGeometry() {
    // Create a simple cube mesh
    const Vertex vertices[] = {
        // Front face
        {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},

        // Back face
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},
        {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}},

        // Top face
        {{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},

        // Bottom face
        {{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-0.5f, -0.5f,  0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},

        // Right face
        {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},

        // Left face
        {{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{-0.5f, -0.5f,  0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{-0.5f,  0.5f,  0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
    };

    const uint32_t indices[] = {
        0, 1, 2,   0, 2, 3,    // Front
        4, 5, 6,   4, 6, 7,    // Back
        8, 9, 10,  8, 10, 11,  // Top
        12, 13, 14, 12, 14, 15, // Bottom
        16, 17, 18, 16, 18, 19, // Right
        20, 21, 22, 20, 22, 23  // Left
    };

    m_indexCount = sizeof(indices) / sizeof(uint32_t);

    // Create vertex buffer
    rhi::BufferDesc vertexBufferDesc;
    vertexBufferDesc.size = sizeof(vertices);
    vertexBufferDesc.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::CopyDst;
    vertexBufferDesc.label = "Cube Vertex Buffer";

    m_vertexBuffer = m_device->createBuffer(vertexBufferDesc);
    m_vertexBuffer->write(vertices, sizeof(vertices));

    // Create index buffer
    rhi::BufferDesc indexBufferDesc;
    indexBufferDesc.size = sizeof(indices);
    indexBufferDesc.usage = rhi::BufferUsage::Index | rhi::BufferUsage::CopyDst;
    indexBufferDesc.label = "Cube Index Buffer";

    m_indexBuffer = m_device->createBuffer(indexBufferDesc);
    m_indexBuffer->write(indices, sizeof(indices));

    std::cout << "[InstancingTest] Cube geometry created: " << m_indexCount << " indices" << std::endl;
}

void InstancingTest::createInstanceData() {
    std::vector<InstanceData> instances(INSTANCE_COUNT);

    // Create a 10x10x10 grid of cubes
    int gridSize = 10;
    float spacing = 2.5f;
    float gridOffset = (gridSize - 1) * spacing * 0.5f;

    for (int i = 0; i < INSTANCE_COUNT; i++) {
        int x = i % gridSize;
        int y = (i / gridSize) % gridSize;
        int z = i / (gridSize * gridSize);

        instances[i].position = glm::vec3(
            x * spacing - gridOffset,
            y * spacing - gridOffset,
            z * spacing - gridOffset
        );

        // Color based on position
        instances[i].color = glm::vec3(
            float(x) / float(gridSize),
            float(y) / float(gridSize),
            float(z) / float(gridSize)
        );

        // Vary scale slightly
        instances[i].scale = 0.8f + 0.4f * float(i % 10) / 10.0f;
        instances[i]._padding = 0.0f;
    }

    // Create instance buffer
    rhi::BufferDesc instanceBufferDesc;
    instanceBufferDesc.size = sizeof(InstanceData) * INSTANCE_COUNT;
    instanceBufferDesc.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::CopyDst;
    instanceBufferDesc.label = "Instance Data Buffer";

    m_instanceBuffer = m_device->createBuffer(instanceBufferDesc);
    m_instanceBuffer->write(instances.data(), instanceBufferDesc.size);

    std::cout << "[InstancingTest] Instance data created: " << INSTANCE_COUNT << " instances" << std::endl;
}

void InstancingTest::createUniformBuffer() {
    rhi::BufferDesc uniformBufferDesc;
    uniformBufferDesc.size = sizeof(CameraUBO);
    uniformBufferDesc.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDst;
    uniformBufferDesc.label = "Camera Uniform Buffer";

    m_uniformBuffer = m_device->createBuffer(uniformBufferDesc);

    std::cout << "[InstancingTest] Uniform buffer created" << std::endl;
}

void InstancingTest::createPipeline() {
    std::cout << "[InstancingTest] Creating pipeline..." << std::endl;

    // Load shaders
    std::cout << "  Loading shaders..." << std::endl;

#ifdef __EMSCRIPTEN__
    // WebGPU/WASM: Use WGSL shaders
    auto vertWgsl = loadSPIRV("shaders/instancing_test.vert.wgsl");
    auto fragWgsl = loadSPIRV("shaders/instancing_test.frag.wgsl");

    rhi::ShaderSource vertSource(rhi::ShaderLanguage::WGSL, vertWgsl, rhi::ShaderStage::Vertex, "main");
    rhi::ShaderSource fragSource(rhi::ShaderLanguage::WGSL, fragWgsl, rhi::ShaderStage::Fragment, "main");
#else
    // Vulkan: Use SPIR-V shaders
    auto vertSpirv = loadSPIRV("shaders/instancing_test.vert.spv");
    auto fragSpirv = loadSPIRV("shaders/instancing_test.frag.spv");

    rhi::ShaderSource vertSource(rhi::ShaderLanguage::SPIRV, vertSpirv, rhi::ShaderStage::Vertex, "main");
    rhi::ShaderSource fragSource(rhi::ShaderLanguage::SPIRV, fragSpirv, rhi::ShaderStage::Fragment, "main");
#endif

    rhi::ShaderDesc vertShaderDesc(vertSource, "Instancing Vertex Shader");
    rhi::ShaderDesc fragShaderDesc(fragSource, "Instancing Fragment Shader");

    m_vertexShader = m_device->createShader(vertShaderDesc);
    m_fragmentShader = m_device->createShader(fragShaderDesc);

    // Create bind group layout (for uniform buffer)
    std::cout << "  Creating bind group layout..." << std::endl;
    rhi::BindGroupLayoutEntry uboEntry(0, rhi::ShaderStage::Vertex, rhi::BindingType::UniformBuffer);

    rhi::BindGroupLayoutDesc bindGroupLayoutDesc;
    bindGroupLayoutDesc.entries = {uboEntry};
    bindGroupLayoutDesc.label = "Camera UBO Layout";

    m_bindGroupLayout = m_device->createBindGroupLayout(bindGroupLayoutDesc);

    // Create bind group (bind actual uniform buffer)
    std::cout << "  Creating bind group..." << std::endl;
    auto bufferEntry = rhi::BindGroupEntry::Buffer(0, m_uniformBuffer.get(), 0, m_uniformBuffer->getSize());

    rhi::BindGroupDesc bindGroupDesc;
    bindGroupDesc.layout = m_bindGroupLayout.get();
    bindGroupDesc.entries = {bufferEntry};
    bindGroupDesc.label = "Camera UBO Bind Group";

    m_bindGroup = m_device->createBindGroup(bindGroupDesc);

    // Create pipeline layout
    std::cout << "  Creating pipeline layout..." << std::endl;
    rhi::PipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.bindGroupLayouts = {m_bindGroupLayout.get()};
    pipelineLayoutDesc.label = "Instancing Pipeline Layout";

    m_pipelineLayout = m_device->createPipelineLayout(pipelineLayoutDesc);

    // Define vertex input layout
    std::cout << "  Setting up vertex layout..." << std::endl;

    // Binding 0: Per-vertex data
    rhi::VertexBufferLayout vertexLayout;
    vertexLayout.stride = sizeof(Vertex);
    vertexLayout.inputRate = rhi::VertexInputRate::Vertex;
    vertexLayout.attributes = {
        {0, 0, rhi::TextureFormat::RGB32Float, offsetof(Vertex, position)},
        {1, 0, rhi::TextureFormat::RGB32Float, offsetof(Vertex, normal)},
        {2, 0, rhi::TextureFormat::RG32Float, offsetof(Vertex, texCoord)}
    };

    // Binding 1: Per-instance data
    rhi::VertexBufferLayout instanceLayout;
    instanceLayout.stride = sizeof(InstanceData);
    instanceLayout.inputRate = rhi::VertexInputRate::Instance;
    instanceLayout.attributes = {
        {3, 1, rhi::TextureFormat::RGB32Float, offsetof(InstanceData, position)},
        {4, 1, rhi::TextureFormat::RGB32Float, offsetof(InstanceData, color)},
        {5, 1, rhi::TextureFormat::R32Float, offsetof(InstanceData, scale)}
    };

    // Create render pipeline
    std::cout << "  Creating render pipeline..." << std::endl;
    std::cout << "  nativeRenderPass: " << m_nativeRenderPass << std::endl;
    rhi::RenderPipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = m_vertexShader.get();
    pipelineDesc.fragmentShader = m_fragmentShader.get();
    pipelineDesc.layout = m_pipelineLayout.get();
    pipelineDesc.vertex.buffers = {vertexLayout, instanceLayout};
    pipelineDesc.nativeRenderPass = m_nativeRenderPass;  // Required for Linux Vulkan (traditional render pass), nullptr for WebGPU

    // Primitive state
    pipelineDesc.primitive.topology = rhi::PrimitiveTopology::TriangleList;
    pipelineDesc.primitive.cullMode = rhi::CullMode::Back;
    pipelineDesc.primitive.frontFace = rhi::FrontFace::CounterClockwise;

    // Depth-stencil state (no depth for now)
    pipelineDesc.depthStencil = nullptr;

    // Color target - must match swapchain format
    // WebGPU only supports BGRA8Unorm, Vulkan uses BGRA8UnormSrgb
    auto backendType = m_device->getBackendType();
    rhi::TextureFormat colorFormat = (backendType == rhi::RHIBackendType::WebGPU)
        ? rhi::TextureFormat::BGRA8Unorm
        : rhi::TextureFormat::BGRA8UnormSrgb;

    pipelineDesc.colorTargets = {
        rhi::ColorTargetState{colorFormat}
    };

    // Multisample state
    pipelineDesc.multisample.sampleCount = 1;

    pipelineDesc.label = "Instancing Pipeline";

    m_pipeline = m_device->createRenderPipeline(pipelineDesc);

    std::cout << "[InstancingTest] Pipeline created successfully!" << std::endl;
}

void InstancingTest::update(float deltaTime) {
    m_time += deltaTime;

    // Update camera yaw for auto-rotation
    if (m_autoRotate) {
        m_cameraYaw += deltaTime * 15.0f; // 15 degrees per second
    }

    // Update camera matrices
    CameraUBO ubo;

    // Calculate camera position from spherical coordinates
    float yawRad = glm::radians(m_cameraYaw);
    float pitchRad = glm::radians(m_cameraPitch);

    float camX = m_cameraDistance * cos(pitchRad) * sin(yawRad);
    float camY = m_cameraDistance * sin(pitchRad);
    float camZ = m_cameraDistance * cos(pitchRad) * cos(yawRad);

    ubo.view = glm::lookAt(
        glm::vec3(camX, camY, camZ),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

    ubo.proj = glm::perspective(
        glm::radians(45.0f),
        float(m_width) / float(m_height),
        0.1f,
        1000.0f
    );

    // Write to uniform buffer
    m_uniformBuffer->write(&ubo, sizeof(CameraUBO));
}

void InstancingTest::render(rhi::RHIRenderPassEncoder* encoder) {
    if (!encoder || !m_pipeline) {
        return;
    }

    // Set pipeline
    encoder->setPipeline(m_pipeline.get());

    // Set viewport and scissor (required for dynamic state)
    encoder->setViewport(0, 0, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f);
    encoder->setScissorRect(0, 0, m_width, m_height);

    // Bind uniform buffer (camera matrices)
    encoder->setBindGroup(0, m_bindGroup.get());

    // Bind vertex buffers
    encoder->setVertexBuffer(0, m_vertexBuffer.get());  // Per-vertex data
    encoder->setVertexBuffer(1, m_instanceBuffer.get()); // Per-instance data

    // Bind index buffer
    encoder->setIndexBuffer(m_indexBuffer.get(), rhi::IndexFormat::Uint32);

    // Draw indexed with instancing
    // This single call renders 1000 cubes!
    encoder->drawIndexed(m_indexCount, INSTANCE_COUNT);
}

void InstancingTest::resize(int width, int height) {
    m_width = width;
    m_height = height;
    std::cout << "[InstancingTest] Resized to " << width << "x" << height << std::endl;
}

void InstancingTest::onMouseMove(double xpos, double ypos) {
    if (m_mousePressed) {
        double deltaX = xpos - m_lastMouseX;
        double deltaY = ypos - m_lastMouseY;

        m_cameraYaw += static_cast<float>(deltaX) * 0.2f;
        m_cameraPitch -= static_cast<float>(deltaY) * 0.2f;

        // Clamp pitch to avoid gimbal lock
        m_cameraPitch = glm::clamp(m_cameraPitch, -89.0f, 89.0f);
    }

    m_lastMouseX = xpos;
    m_lastMouseY = ypos;
}

void InstancingTest::onMouseButton(int button, int action) {
    if (button == 0) { // Left mouse button
        if (action == 1) { // Press
            m_mousePressed = true;
            m_autoRotate = false; // Stop auto-rotation when user controls camera
        } else if (action == 0) { // Release
            m_mousePressed = false;
        }
    }
}

void InstancingTest::onKeyPress(int key, int action) {
    if (action != 1) return; // Only on key press, not release or repeat

    // W/S: zoom in/out
    if (key == 87) { // W
        m_cameraDistance = glm::max(10.0f, m_cameraDistance - 5.0f);
    } else if (key == 83) { // S
        m_cameraDistance = glm::min(200.0f, m_cameraDistance + 5.0f);
    }
    // R: reset camera
    else if (key == 82) { // R
        m_cameraDistance = 50.0f;
        m_cameraYaw = 0.0f;
        m_cameraPitch = 20.0f;
        m_autoRotate = true;
    }
    // Space: toggle auto-rotation
    else if (key == 32) { // Space
        m_autoRotate = !m_autoRotate;
    }
}

} // namespace examples
