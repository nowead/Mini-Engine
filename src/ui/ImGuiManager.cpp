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

void ImGuiManager::renderUI(Camera& camera, bool isFdfMode, float zScale,
                            std::function<void()> onModeToggle,
                            std::function<void(const std::string&)> onFileLoad) {
    // Main control window - fixed to top-left corner
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::Begin("FdF Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);

    ImGui::Text("Vulkan FdF Wireframe Visualizer");
    ImGui::Separator();

    // Mode control
    ImGui::Text("Rendering Mode:");
    if (ImGui::Button(isFdfMode ? "Mode: FDF (Wireframe)" : "Mode: OBJ (Solid)")) {
        if (onModeToggle) onModeToggle();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Toggle between FDF wireframe and OBJ solid rendering");
    }

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

        // Display Z-scale
        if (isFdfMode) {
            ImGui::Separator();
            ImGui::Text("Z-Axis Scale:");
            ImGui::Text("  Scale: %.2f", zScale);
            ImGui::TextDisabled("  (Q/E keys to adjust)");
        }

        ImGui::Separator();
        ImGui::Text("Speed Controls:");
        ImGui::SliderFloat("Move Speed", &moveSpeed, 0.1f, 5.0f);
        ImGui::SliderFloat("Rotate Speed", &rotateSpeed, 0.1f, 2.0f);
        ImGui::SliderFloat("Zoom Speed", &zoomSpeed, 0.1f, 3.0f);
    }

    ImGui::Separator();

    // File loading
    if (ImGui::CollapsingHeader("File Loading")) {
        ImGui::InputText("File Path", filePathBuffer, sizeof(filePathBuffer));
        if (ImGui::Button("Load File")) {
            if (onFileLoad) onFileLoad(std::string(filePathBuffer));
        }

        ImGui::Text("Quick Load:");
        if (ImGui::Button("test.fdf")) {
            if (onFileLoad) onFileLoad("models/test.fdf");
        }
        ImGui::SameLine();
        if (ImGui::Button("pyramid.fdf")) {
            if (onFileLoad) onFileLoad("models/pyramid.fdf");
        }
    }

    ImGui::Separator();

    // Controls help
    if (ImGui::CollapsingHeader("Controls Help")) {
        ImGui::BulletText("Left Mouse + Drag: Rotate camera");
        ImGui::BulletText("Mouse Wheel: Zoom in/out");
        ImGui::BulletText("W/A/S/D: Move camera");
        ImGui::BulletText("Q/E: Adjust Z-axis scale (FDF mode)");
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
