/**
 * @file rhi_smoke_test.cpp
 * @brief RHI Smoke Test - RHI Factory, Bridge & Command Recording
 *
 * Tests:
 * 1. RHIFactory::createDevice() works
 * 2. Backend enumeration
 * 3. RendererBridge initialization
 * 4. Basic device queries
 * 5. Command encoding
 * 6. Queue submission
 */

#include <rhi/RHI.hpp>
#include "RHIFactory.hpp"
#include "src/rendering/RendererBridge.hpp"
#include <GLFW/glfw3.h>
#include <iostream>
#include <memory>

void printBackendInfo() {
    std::cout << "\n=== Available RHI Backends ===\n";
    auto backends = rhi::RHIFactory::getAvailableBackends();
    
    for (const auto& backend : backends) {
        std::cout << "  " << backend.name;
        if (backend.available) {
            std::cout << " ✓ Available\n";
        } else {
            std::cout << " ✗ Unavailable (" << backend.unavailableReason << ")\n";
        }
    }
    
    auto defaultBackend = rhi::RHIFactory::getDefaultBackend();
    std::cout << "\nDefault Backend: " 
              << rhi::RHIFactory::getBackendName(defaultBackend) << "\n";
}

bool testRHIFactory(GLFWwindow* window) {
    std::cout << "\n=== Test 1: RHI Factory ===\n";
    
    try {
        auto createInfo = rhi::DeviceCreateInfo{}
            .setBackend(rhi::RHIBackendType::Vulkan)
            .setValidation(true)
            .setWindow(window)
            .setAppName("RHI Smoke Test");
        
        auto device = rhi::RHIFactory::createDevice(createInfo);
        
        if (!device) {
            std::cerr << "✗ Failed to create device\n";
            return false;
        }
        
        std::cout << "✓ RHIFactory::createDevice() succeeded\n";
        std::cout << "  Backend: " << rhi::RHIFactory::getBackendName(device->getBackendType()) << "\n";
        std::cout << "  Device: " << device->getDeviceName() << "\n";
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "✗ Exception: " << e.what() << "\n";
        return false;
    }
}

bool testRendererBridge(GLFWwindow* window) {
    std::cout << "\n=== Test 2: Renderer Bridge ===\n";
    
    try {
        auto bridge = std::make_unique<rendering::RendererBridge>(window, true);
        
        if (!bridge->isReady()) {
            std::cerr << "✗ Bridge not ready\n";
            return false;
        }
        
        std::cout << "✓ RendererBridge initialized\n";
        
        auto* device = bridge->getDevice();
        if (!device) {
            std::cerr << "✗ Device is null\n";
            return false;
        }
        
        std::cout << "✓ Device accessible via bridge\n";
        std::cout << "  Backend: " << rhi::RHIFactory::getBackendName(device->getBackendType()) << "\n";
        std::cout << "  Device: " << device->getDeviceName() << "\n";
        
        // Test swapchain creation
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        bridge->createSwapchain(width, height, true);
        
        if (!bridge->getSwapchain()) {
            std::cerr << "✗ Failed to create swapchain\n";
            return false;
        }
        
        std::cout << "✓ Swapchain created (" << width << "x" << height << ")\n";
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "✗ Exception: " << e.what() << "\n";
        return false;
    }
}

bool testResourceCreation(GLFWwindow* window) {
    std::cout << "\n=== Test 3: Resource Creation ===\n";
    
    try {
        auto bridge = std::make_unique<rendering::RendererBridge>(window, true);
        auto* device = bridge->getDevice();
        
        // Test buffer creation
        rhi::BufferDesc bufferDesc;
        bufferDesc.size = 1024;
        bufferDesc.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::CopyDst;
        
        auto buffer = device->createBuffer(bufferDesc);
        if (!buffer) {
            std::cerr << "✗ Failed to create buffer\n";
            return false;
        }
        std::cout << "✓ Buffer created (1024 bytes)\n";
        
        // Test fence creation
        auto fence = device->createFence(false);
        if (!fence) {
            std::cerr << "✗ Failed to create fence\n";
            return false;
        }
        std::cout << "✓ Fence created\n";
        
        // Test semaphore creation
        auto semaphore = device->createSemaphore();
        if (!semaphore) {
            std::cerr << "✗ Failed to create semaphore\n";
            return false;
        }
        std::cout << "✓ Semaphore created\n";
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "✗ Exception: " << e.what() << "\n";
        return false;
    }
}

bool testCommandEncoding(GLFWwindow* window) {
    std::cout << "\n=== Test 4: Command Encoding ===\n";
    
    try {
        auto bridge = std::make_unique<rendering::RendererBridge>(window, true);
        auto* device = bridge->getDevice();
        
        // Create command encoder
        auto encoder = device->createCommandEncoder();
        if (!encoder) {
            std::cerr << "✗ Failed to create command encoder\n";
            return false;
        }
        std::cout << "✓ Command encoder created\n";
        
        // Finish encoding (creates empty command buffer)
        auto commandBuffer = encoder->finish();
        if (!commandBuffer) {
            std::cerr << "✗ Failed to finish command buffer\n";
            return false;
        }
        std::cout << "✓ Command buffer finished\n";
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "✗ Exception: " << e.what() << "\n";
        return false;
    }
}

bool testPipelineCreation(GLFWwindow* window) {
    std::cout << "\n=== Test 6: Pipeline Creation ===\n";
    
    try {
        auto bridge = std::make_unique<rendering::RendererBridge>(window, true);
        auto* device = bridge->getDevice();
        
        // Create shader from SPIR-V file
        auto vertShader = bridge->createShaderFromFile(
            "shaders/slang.spv", 
            rhi::ShaderStage::Vertex, 
            "vertMain"
        );
        if (!vertShader) {
            std::cerr << "✗ Failed to create vertex shader\n";
            return false;
        }
        std::cout << "✓ Vertex shader created from SPIR-V\n";
        
        auto fragShader = bridge->createShaderFromFile(
            "shaders/slang.spv",
            rhi::ShaderStage::Fragment,
            "fragMain"
        );
        if (!fragShader) {
            std::cerr << "✗ Failed to create fragment shader\n";
            return false;
        }
        std::cout << "✓ Fragment shader created from SPIR-V\n";
        
        // Create bind group layout
        rhi::BindGroupLayoutDesc layoutDesc;
        rhi::BindGroupLayoutEntry uboEntry;
        uboEntry.binding = 0;
        uboEntry.type = rhi::BindingType::UniformBuffer;
        uboEntry.visibility = rhi::ShaderStage::Vertex;
        layoutDesc.entries.push_back(uboEntry);
        
        auto bindGroupLayout = device->createBindGroupLayout(layoutDesc);
        if (!bindGroupLayout) {
            std::cerr << "✗ Failed to create bind group layout\n";
            return false;
        }
        std::cout << "✓ Bind group layout created\n";
        
        // Create pipeline layout
        rhi::PipelineLayoutDesc pipelineLayoutDesc;
        pipelineLayoutDesc.bindGroupLayouts.push_back(bindGroupLayout.get());
        
        auto pipelineLayout = bridge->createPipelineLayout(pipelineLayoutDesc);
        if (!pipelineLayout) {
            std::cerr << "✗ Failed to create pipeline layout\n";
            return false;
        }
        std::cout << "✓ Pipeline layout created\n";
        
        // Create render pipeline
        rhi::RenderPipelineDesc pipelineDesc;
        pipelineDesc.label = "Test Pipeline";
        pipelineDesc.vertexShader = vertShader.get();
        pipelineDesc.fragmentShader = fragShader.get();
        pipelineDesc.layout = pipelineLayout.get();
        
        // Vertex layout (position, color, texCoord)
        rhi::VertexBufferLayout vertexBufferLayout;
        vertexBufferLayout.stride = sizeof(float) * 8; // 3 + 3 + 2
        vertexBufferLayout.inputRate = rhi::VertexInputRate::Vertex;
        
        // Position attribute - vec3
        rhi::VertexAttribute posAttr;
        posAttr.location = 0;
        posAttr.binding = 0;
        posAttr.format = rhi::TextureFormat::RGB32Float;
        posAttr.offset = 0;
        vertexBufferLayout.attributes.push_back(posAttr);
        
        // Color attribute - vec3
        rhi::VertexAttribute colorAttr;
        colorAttr.location = 1;
        colorAttr.binding = 0;
        colorAttr.format = rhi::TextureFormat::RGB32Float;
        colorAttr.offset = sizeof(float) * 3;
        vertexBufferLayout.attributes.push_back(colorAttr);
        
        // TexCoord attribute - vec2
        rhi::VertexAttribute texAttr;
        texAttr.location = 2;
        texAttr.binding = 0;
        texAttr.format = rhi::TextureFormat::RG32Float;
        texAttr.offset = sizeof(float) * 6;
        vertexBufferLayout.attributes.push_back(texAttr);
        
        pipelineDesc.vertex.buffers.push_back(vertexBufferLayout);
        
        // Color target
        rhi::ColorTargetState colorTarget;
        colorTarget.format = rhi::TextureFormat::BGRA8Unorm;
        pipelineDesc.colorTargets.push_back(colorTarget);
        
        // Depth state
        rhi::DepthStencilState depthState;
        depthState.format = rhi::TextureFormat::Depth32Float;
        depthState.depthWriteEnabled = true;
        depthState.depthCompare = rhi::CompareOp::Less;
        pipelineDesc.depthStencil = &depthState;
        
        // Primitive state
        pipelineDesc.primitive.topology = rhi::PrimitiveTopology::TriangleList;
        pipelineDesc.primitive.cullMode = rhi::CullMode::Back;
        pipelineDesc.primitive.frontFace = rhi::FrontFace::CounterClockwise;
        
        auto pipeline = bridge->createRenderPipeline(pipelineDesc);
        if (!pipeline) {
            std::cerr << "✗ Failed to create render pipeline\n";
            return false;
        }
        std::cout << "✓ Render pipeline created\n";
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "✗ Exception: " << e.what() << "\n";
        return false;
    }
}

bool testQueueSubmission(GLFWwindow* window) {
    std::cout << "\n=== Test 5: Queue Submission ===\n";
    
    try {
        auto bridge = std::make_unique<rendering::RendererBridge>(window, true);
        auto* device = bridge->getDevice();
        
        // Get graphics queue
        auto* queue = device->getQueue(rhi::QueueType::Graphics);
        if (!queue) {
            std::cerr << "✗ Failed to get graphics queue\n";
            return false;
        }
        std::cout << "✓ Graphics queue obtained\n";
        
        // Create fence for synchronization
        auto fence = device->createFence(false);
        
        // Create and finish command buffer
        auto encoder = device->createCommandEncoder();
        auto commandBuffer = encoder->finish();
        
        // Submit command buffer with fence
        queue->submit(commandBuffer.get(), fence.get());
        std::cout << "✓ Command buffer submitted\n";
        
        // Wait for completion
        fence->wait();
        std::cout << "✓ Fence signaled (GPU work complete)\n";
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "✗ Exception: " << e.what() << "\n";
        return false;
    }
}

int main() {
    std::cout << "\n========================================\n";
    std::cout << "  MiniEngine RHI Smoke Test\n";
    std::cout << "========================================\n";
    
    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "✗ Failed to initialize GLFW\n";
        return 1;
    }
    
    // Create window (offscreen for testing)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(800, 600, "RHI Smoke Test", nullptr, nullptr);
    
    if (!window) {
        std::cerr << "✗ Failed to create GLFW window\n";
        glfwTerminate();
        return 1;
    }
    
    // Run tests
    printBackendInfo();
    
    bool test1 = testRHIFactory(window);
    bool test2 = testRendererBridge(window);
    bool test3 = testResourceCreation(window);
    bool test4 = testCommandEncoding(window);
    bool test5 = testQueueSubmission(window);
    bool test6 = testPipelineCreation(window);
    
    // Cleanup
    glfwDestroyWindow(window);
    glfwTerminate();
    
    // Results
    std::cout << "\n========================================\n";
    std::cout << "  Test Results\n";
    std::cout << "========================================\n";
    std::cout << "  RHI Factory:       " << (test1 ? "PASS" : "FAIL") << "\n";
    std::cout << "  Renderer Bridge:   " << (test2 ? "PASS" : "FAIL") << "\n";
    std::cout << "  Resource Creation: " << (test3 ? "PASS" : "FAIL") << "\n";
    std::cout << "  Command Encoding:  " << (test4 ? "PASS" : "FAIL") << "\n";
    std::cout << "  Queue Submission:  " << (test5 ? "PASS" : "FAIL") << "\n";
    std::cout << "  Pipeline Creation: " << (test6 ? "PASS" : "FAIL") << "\n";
    std::cout << "========================================\n";
    
    bool allPassed = test1 && test2 && test3 && test4 && test5 && test6;
    if (allPassed) {
        std::cout << "\nRHI Smoke Test: ALL TESTS PASSED\n";
        return 0;
    } else {
        std::cout << "\nRHI Smoke Test: SOME TESTS FAILED\n";
        return 1;
    }
}
