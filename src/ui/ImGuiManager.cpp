#include "ImGuiManager.hpp"
#include "ImGuiVulkanBackend.hpp"
#include <imgui.h>
#include <stdexcept>

ImGuiManager::ImGuiManager(GLFWwindow* window,
                           rhi::RHIDevice* device,
                           rhi::RHISwapchain* swapchain) {
    // Select backend based on RHI backend type
    switch (device->getBackendType()) {
        case rhi::RHIBackendType::Vulkan:
            backend = std::make_unique<ui::ImGuiVulkanBackend>();
            break;
        case rhi::RHIBackendType::WebGPU:
            // Future: backend = std::make_unique<ui::ImGuiWebGPUBackend>();
            throw std::runtime_error("WebGPU ImGui backend not yet implemented");
        default:
            throw std::runtime_error("Unsupported RHI backend for ImGui");
    }

    // Initialize the selected backend
    backend->init(window, device, swapchain);
}

ImGuiManager::~ImGuiManager() {
    if (backend) {
        backend->shutdown();
    }
}

void ImGuiManager::newFrame() {
    backend->newFrame();
}

void ImGuiManager::renderUI(Camera& camera, uint32_t buildingCount) {
    // Main control window - fixed to top-left corner
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::Begin("Mini-Engine", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);

    ImGui::Text("Building Visualization Engine");
    ImGui::Separator();

    // Camera controls
    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Projection mode
        ProjectionMode currentMode = camera.getProjectionMode();
        const char* projectionModes[] = { "Perspective", "Isometric" };
        int currentProjection = (currentMode == ProjectionMode::Perspective) ? 0 : 1;

        if (ImGui::Combo("Projection", &currentProjection, projectionModes, 2)) {
            camera.setProjectionMode(
                currentProjection == 0 ? ProjectionMode::Perspective : ProjectionMode::Isometric
            );
        }

        // Reset camera
        if (ImGui::Button("Reset Camera")) {
            camera.reset();
        }
    }

    ImGui::Separator();

    // Scene info
    if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Buildings: %u", buildingCount);
        ImGui::Text("Rendering: GPU Instancing");
    }

    ImGui::Separator();

    // Controls help
    if (ImGui::CollapsingHeader("Controls")) {
        ImGui::BulletText("Left Mouse + Drag: Rotate camera");
        ImGui::BulletText("Mouse Wheel: Zoom in/out");
        ImGui::BulletText("W/A/S/D: Move camera");
        ImGui::BulletText("P or I: Toggle projection");
        ImGui::BulletText("R: Reset camera");
        ImGui::BulletText("ESC: Exit");
    }

    ImGui::Separator();

    // Statistics
    if (ImGui::CollapsingHeader("Statistics")) {
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Text("Frame Time: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);
    }

    // Demo window toggle
    ImGui::Separator();
    ImGui::Checkbox("Show ImGui Demo", &showDemoWindow);

    ImGui::End();

    // Show demo window if enabled
    if (showDemoWindow) {
        ImGui::ShowDemoWindow(&showDemoWindow);
    }
}

void ImGuiManager::render(rhi::RHICommandEncoder* encoder, uint32_t imageIndex) {
    backend->render(encoder, imageIndex);
}

void ImGuiManager::handleResize() {
    backend->handleResize();
}
