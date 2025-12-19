/**
 * @file rhi_smoke_test.cpp
 * @brief Phase 3 Smoke Test - RHI Factory & Integration Bridge
 *
 * Tests:
 * 1. RHIFactory::createDevice() works
 * 2. Backend enumeration
 * 3. RendererBridge initialization
 * 4. Basic device queries
 */

#include "src/rhi/RHI.hpp"
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

int main() {
    std::cout << "╔════════════════════════════════════════╗\n";
    std::cout << "║   Phase 3: RHI Factory Smoke Test     ║\n";
    std::cout << "╚════════════════════════════════════════╝\n";
    
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
    
    // Cleanup
    glfwDestroyWindow(window);
    glfwTerminate();
    
    // Results
    std::cout << "\n╔════════════════════════════════════════╗\n";
    std::cout << "║          Test Results                  ║\n";
    std::cout << "╠════════════════════════════════════════╣\n";
    std::cout << "║ RHI Factory:        " << (test1 ? "✓ PASS" : "✗ FAIL") << "              ║\n";
    std::cout << "║ Renderer Bridge:    " << (test2 ? "✓ PASS" : "✗ FAIL") << "              ║\n";
    std::cout << "║ Resource Creation:  " << (test3 ? "✓ PASS" : "✗ FAIL") << "              ║\n";
    std::cout << "╚════════════════════════════════════════╝\n";
    
    bool allPassed = test1 && test2 && test3;
    if (allPassed) {
        std::cout << "\n🎉 Phase 3 Smoke Test: ALL TESTS PASSED\n";
        return 0;
    } else {
        std::cout << "\n❌ Phase 3 Smoke Test: SOME TESTS FAILED\n";
        return 1;
    }
}
