#pragma once

#include "ImGuiBackend.hpp"
#include "src/rhi/RHI.hpp"
#include "src/scene/Camera.hpp"

#include <GLFW/glfw3.h>
#include <functional>
#include <string>
#include <memory>

/**
 * @brief ImGui UI manager (Application layer)
 *
 * Responsibilities:
 * - ImGui backend management (Vulkan, WebGPU, etc.)
 * - UI rendering (camera controls, file loading, statistics)
 * - Backend-agnostic ImGui integration via adapter pattern
 *
 * Note: Migrated to RHI in Phase 6 (ImGui Layer Migration)
 */
class ImGuiManager {
public:
    ImGuiManager(GLFWwindow* window,
                 rhi::RHIDevice* device,
                 rhi::RHISwapchain* swapchain);
    ~ImGuiManager();

    // Disable copy and move
    ImGuiManager(const ImGuiManager&) = delete;
    ImGuiManager& operator=(const ImGuiManager&) = delete;

    void newFrame();
    void renderUI(Camera& camera, bool isFdfMode, float zScale,
                  std::function<void()> onModeToggle,
                  std::function<void(const std::string&)> onFileLoad);
    void render(rhi::RHICommandEncoder* encoder, uint32_t imageIndex);
    void handleResize();

private:
    std::unique_ptr<ui::ImGuiBackend> backend;

    // UI state
    bool showDemoWindow = false;
    char filePathBuffer[256] = "models/test.fdf";
    float moveSpeed = 1.0f;
    float rotateSpeed = 0.5f;
    float zoomSpeed = 1.0f;
};
