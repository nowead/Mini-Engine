/**
 * @file instancing_test_main.cpp
 * @brief GPU Instancing Demo - Renders 1000 cubes with a single draw call
 *
 * Phase 1.1: Core Performance Infrastructure
 * Demonstrates GPU instancing capability for rendering thousands of objects efficiently.
 *
 * Performance Target: 1000 instances @ 60 FPS
 */

#include <rhi/RHI.hpp>
#include "RHIFactory.hpp"
#include "src/rendering/RendererBridge.hpp"
#include "src/examples/InstancingTest.hpp"
#include <GLFW/glfw3.h>
#include <iostream>
#include <memory>
#include <chrono>

// For Emscripten main loop
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

// For platform-specific Vulkan code
#if !defined(__EMSCRIPTEN__)
#include <vulkan/vulkan.h>
#include <rhi/vulkan/VulkanRHISwapchain.hpp>
#include <rhi/vulkan/VulkanRHICommandEncoder.hpp>
#endif

constexpr int WINDOW_WIDTH = 1280;
constexpr int WINDOW_HEIGHT = 720;

class InstancingDemo {
public:
    InstancingDemo() : m_window(nullptr), m_lastTime(0.0) {}

    ~InstancingDemo() {
        m_instancingTest.reset();
        m_bridge.reset();

        if (m_window) {
            glfwDestroyWindow(m_window);
        }
        glfwTerminate();
    }

    bool init() {
        // Initialize GLFW
        if (!glfwInit()) {
            std::cerr << "Failed to initialize GLFW\n";
            return false;
        }

        // Create window
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        m_window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT,
                                    "GPU Instancing Test - 1000 Cubes",
                                    nullptr, nullptr);
        if (!m_window) {
            std::cerr << "Failed to create GLFW window\n";
            return false;
        }

        std::cout << "=== GPU Instancing Test ===\n";
        std::cout << "Window created: " << WINDOW_WIDTH << "x" << WINDOW_HEIGHT << "\n\n";

        // Create renderer bridge
        try {
            m_bridge = std::make_unique<rendering::RendererBridge>(m_window, true);

            if (!m_bridge->isReady()) {
                std::cerr << "Renderer bridge not ready\n";
                return false;
            }

            auto* device = m_bridge->getDevice();
            std::cout << "RHI Device created:\n";
            std::cout << "  Backend: " << rhi::RHIFactory::getBackendName(device->getBackendType()) << "\n";
            std::cout << "  Device: " << device->getDeviceName() << "\n\n";

            // Create swapchain
            int width, height;
            glfwGetFramebufferSize(m_window, &width, &height);
            m_bridge->createSwapchain(width, height, true);

            if (!m_bridge->getSwapchain()) {
                std::cerr << "Failed to create swapchain\n";
                return false;
            }

            std::cout << "Swapchain created: " << width << "x" << height << "\n\n";

            // Get native render pass (for Linux Vulkan traditional render pass)
            // WebGPU doesn't need traditional render pass
            void* nativeRenderPass = nullptr;
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
            // On Linux with Vulkan, we need to provide the VkRenderPass for pipeline creation
            auto* vulkanSwapchain = static_cast<RHI::Vulkan::VulkanRHISwapchain*>(m_bridge->getSwapchain());
            VkRenderPass vkRenderPass = static_cast<VkRenderPass>(vulkanSwapchain->getRenderPass());
            nativeRenderPass = reinterpret_cast<void*>(vkRenderPass);
            std::cout << "VkRenderPass handle: " << vkRenderPass << " (ptr: " << nativeRenderPass << ")\n";
#endif

            // Create instancing test
            m_instancingTest = std::make_unique<examples::InstancingTest>(
                device, width, height, nativeRenderPass);
            m_instancingTest->init();

            // Store initial size to prevent unnecessary swapchain recreation
            m_width = width;
            m_height = height;

            // Set up GLFW callbacks for camera controls
            glfwSetWindowUserPointer(m_window, this);

            glfwSetCursorPosCallback(m_window, [](GLFWwindow* window, double xpos, double ypos) {
                auto* demo = static_cast<InstancingDemo*>(glfwGetWindowUserPointer(window));
                if (demo && demo->m_instancingTest) {
                    demo->m_instancingTest->onMouseMove(xpos, ypos);
                }
            });

            glfwSetMouseButtonCallback(m_window, [](GLFWwindow* window, int button, int action, int mods) {
                auto* demo = static_cast<InstancingDemo*>(glfwGetWindowUserPointer(window));
                if (demo && demo->m_instancingTest) {
                    demo->m_instancingTest->onMouseButton(button, action);
                }
            });

            glfwSetKeyCallback(m_window, [](GLFWwindow* window, int key, int scancode, int action, int mods) {
                auto* demo = static_cast<InstancingDemo*>(glfwGetWindowUserPointer(window));
                if (demo && demo->m_instancingTest) {
                    demo->m_instancingTest->onKeyPress(key, action);
                }
            });

            std::cout << "\n=== Initialization Complete ===\n";
            std::cout << "\n=== Controls ===\n";
            std::cout << "  Left Mouse Drag: Rotate camera\n";
            std::cout << "  W/S: Zoom in/out\n";
            std::cout << "  R: Reset camera\n";
            std::cout << "  Space: Toggle auto-rotation\n";
            std::cout << "  ESC: Exit\n\n";

            m_lastTime = glfwGetTime();
            m_frameCount = 0;
            m_fpsTimer = 0.0;

            return true;
        } catch (const std::exception& e) {
            std::cerr << "Exception during initialization: " << e.what() << "\n";
            return false;
        }
    }

    void mainLoop() {
        glfwPollEvents();

        // Calculate delta time
        double currentTime = glfwGetTime();
        float deltaTime = static_cast<float>(currentTime - m_lastTime);
        m_lastTime = currentTime;

        // Update FPS counter
        m_frameCount++;
        m_fpsTimer += deltaTime;
        if (m_fpsTimer >= 1.0) {
            double fps = m_frameCount / m_fpsTimer;
            std::cout << "FPS: " << fps << " (1000 instances, 1 draw call)\n";
            m_frameCount = 0;
            m_fpsTimer = 0.0;
        }

        // Handle window resize
        int width, height;
        glfwGetFramebufferSize(m_window, &width, &height);
        if (width != m_width || height != m_height) {
            m_width = width;
            m_height = height;
            m_bridge->createSwapchain(width, height, true);
            m_instancingTest->resize(width, height);
            std::cout << "Window resized: " << width << "x" << height << "\n";
        }

        // Update
        m_instancingTest->update(deltaTime);

        // Render
        render();

        // ESC to exit
        if (glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(m_window, GLFW_TRUE);
#ifdef __EMSCRIPTEN__
            emscripten_cancel_main_loop();
#endif
        }
    }

    void run() {
#ifdef __EMSCRIPTEN__
        // WebGPU: Use emscripten_set_main_loop for browser's requestAnimationFrame
        emscripten_set_main_loop_arg(
            [](void* arg) {
                auto* demo = static_cast<InstancingDemo*>(arg);
                demo->mainLoop();
            },
            this,
            0,  // Use browser's requestAnimationFrame (typically 60 FPS)
            1   // Simulate infinite loop
        );
#else
        // Native: Traditional game loop
        while (!glfwWindowShouldClose(m_window)) {
            mainLoop();
        }

        std::cout << "\n=== Shutting down ===\n";
#endif
    }

private:
    void render() {
        auto* swapchain = m_bridge->getSwapchain();
        if (!swapchain) return;

        // Acquire next image with synchronization
        auto* imageAvailableSemaphore = m_bridge->getImageAvailableSemaphore();
        auto textureView = swapchain->acquireNextImage(imageAvailableSemaphore);
        if (!textureView) {
            std::cerr << "Failed to acquire next image\n";
            return;
        }

        // Create command encoder
        auto* device = m_bridge->getDevice();
        auto encoder = device->createCommandEncoder();

        // Transition swapchain image to COLOR_ATTACHMENT_OPTIMAL (Mac Vulkan with dynamic rendering only)
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
        auto* vulkanSwapchain = static_cast<RHI::Vulkan::VulkanRHISwapchain*>(swapchain);
        auto* vulkanEncoder = static_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(encoder.get());

        // Transition from UNDEFINED/PRESENT_SRC to COLOR_ATTACHMENT_OPTIMAL
        vk::ImageMemoryBarrier barrier;
        barrier.oldLayout = vk::ImageLayout::eUndefined;  // Can transition from UNDEFINED or PRESENT_SRC
        barrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = vulkanSwapchain->getCurrentVkImage();
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

        vulkanEncoder->getCommandBuffer().pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eColorAttachmentOutput,
            {},
            nullptr,
            nullptr,
            barrier
        );
#endif

        // Begin render pass
        rhi::RenderPassColorAttachment colorAttachment;
        colorAttachment.view = textureView;
        colorAttachment.loadOp = rhi::LoadOp::Clear;
        colorAttachment.storeOp = rhi::StoreOp::Store;
        colorAttachment.clearValue = rhi::ClearColorValue(0.1f, 0.1f, 0.1f, 1.0f);

        rhi::RenderPassDesc renderPassDesc;
        renderPassDesc.colorAttachments = {colorAttachment};
        renderPassDesc.width = m_width;
        renderPassDesc.height = m_height;

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
        // Linux Vulkan: Provide native render pass and framebuffer for traditional rendering
        // WebGPU uses dynamic rendering, doesn't need this
        auto* vulkanSwapchain = static_cast<RHI::Vulkan::VulkanRHISwapchain*>(swapchain);
        VkRenderPass vkRenderPass = static_cast<VkRenderPass>(vulkanSwapchain->getRenderPass());
        uint32_t imageIndex = vulkanSwapchain->getCurrentImageIndex();
        VkFramebuffer vkFramebuffer = static_cast<VkFramebuffer>(vulkanSwapchain->getFramebuffer(imageIndex));
        renderPassDesc.nativeRenderPass = reinterpret_cast<void*>(vkRenderPass);
        renderPassDesc.nativeFramebuffer = reinterpret_cast<void*>(vkFramebuffer);
#endif

        auto renderPass = encoder->beginRenderPass(renderPassDesc);

        // Render instancing test
        m_instancingTest->render(renderPass.get());

        // End render pass
        renderPass->end();

        // Transition swapchain image to PRESENT_SRC layout (Mac Vulkan with dynamic rendering only)
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
        auto* vulkanSwapchain2 = static_cast<RHI::Vulkan::VulkanRHISwapchain*>(swapchain);
        auto* vulkanEncoder2 = static_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(encoder.get());
        vulkanEncoder2->transitionImageLayoutForPresent(vulkanSwapchain2->getCurrentVkImage());
#endif

        // Finish encoding and submit with proper synchronization
        auto commandBuffer = encoder->finish();
        auto* queue = device->getQueue(rhi::QueueType::Graphics);

        // Submit with semaphore synchronization:
        // - Wait on imageAvailableSemaphore (signaled by acquireNextImage)
        // - Signal renderFinishedSemaphore when rendering is complete
        auto* renderFinishedSemaphore = m_bridge->getRenderFinishedSemaphore();
        queue->submit(commandBuffer.get(), imageAvailableSemaphore, renderFinishedSemaphore);

#ifndef __EMSCRIPTEN__
        // Native Vulkan: Explicit present call
        // WebGPU: Present is automatic via emscripten_set_main_loop / requestAnimationFrame
        swapchain->present(renderFinishedSemaphore);
#endif
    }

    GLFWwindow* m_window;
    std::unique_ptr<rendering::RendererBridge> m_bridge;
    std::unique_ptr<examples::InstancingTest> m_instancingTest;

    double m_lastTime;
    int m_frameCount = 0;
    double m_fpsTimer = 0.0;
    int m_width = WINDOW_WIDTH;
    int m_height = WINDOW_HEIGHT;
};

int main() {
    try {
        InstancingDemo demo;

        if (!demo.init()) {
            return EXIT_FAILURE;
        }

        demo.run();

        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
