#pragma once

#include "src/core/VulkanDevice.hpp"
#include "src/rendering/VulkanSwapchain.hpp"
#include "src/core/CommandManager.hpp"
#include "src/scene/Camera.hpp"

#include <GLFW/glfw3.h>
#include <functional>
#include <string>

/**
 * @brief ImGui UI manager (Application layer)
 *
 * Responsibilities:
 * - ImGui context management
 * - UI rendering (camera controls, file loading, statistics)
 * - Platform-specific rendering (dynamic rendering on macOS, render pass on Linux)
 */
class ImGuiManager {
public:
    ImGuiManager(GLFWwindow* window,
                 VulkanDevice& device,
                 VulkanSwapchain& swapchain,
                 CommandManager& commandManager);
    ~ImGuiManager();

    // Disable copy and move
    ImGuiManager(const ImGuiManager&) = delete;
    ImGuiManager& operator=(const ImGuiManager&) = delete;

    void newFrame();
    void renderUI(Camera& camera, bool isFdfMode, float zScale,
                  std::function<void()> onModeToggle,
                  std::function<void(const std::string&)> onFileLoad);
    void render(const vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex);
    void handleResize();

private:
    VulkanDevice& device;
    VulkanSwapchain& swapchain;
    CommandManager& commandManager;

    vk::raii::DescriptorPool imguiPool = nullptr;

    // UI state
    bool showDemoWindow = false;
    char filePathBuffer[256] = "models/test.fdf";
    float moveSpeed = 1.0f;
    float rotateSpeed = 0.5f;
    float zoomSpeed = 1.0f;

    void createDescriptorPool();
    void initImGui(GLFWwindow* window);
    void cleanup();
};
