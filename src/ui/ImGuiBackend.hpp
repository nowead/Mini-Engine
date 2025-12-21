#pragma once

#include <rhi/RHI.hpp>
#include <GLFW/glfw3.h>

namespace ui {

/**
 * @brief Abstract interface for ImGui backend implementations
 *
 * This interface abstracts platform-specific ImGui rendering backends
 * (Vulkan, WebGPU, D3D12, Metal) to work with the RHI abstraction layer.
 *
 * Each backend is responsible for:
 * - Initializing ImGui with platform-specific details
 * - Creating necessary GPU resources (descriptor pools, textures, etc.)
 * - Rendering ImGui draw data to command buffers
 * - Handling window resize events
 * - Cleanup on shutdown
 *
 * Note: Migrated to RHI in Phase 6 (ImGui Layer Migration)
 */
class ImGuiBackend {
public:
    virtual ~ImGuiBackend() = default;

    /**
     * @brief Initialize ImGui backend
     * @param window GLFW window handle
     * @param device RHI device for GPU resource creation
     * @param swapchain RHI swapchain for render target info
     *
     * This method should:
     * 1. Initialize platform/renderer backends (GLFW + graphics API)
     * 2. Create descriptor pools and other GPU resources
     * 3. Upload font textures to GPU
     */
    virtual void init(GLFWwindow* window,
                     rhi::RHIDevice* device,
                     rhi::RHISwapchain* swapchain) = 0;

    /**
     * @brief Begin new ImGui frame
     *
     * Call this at the start of each frame before any ImGui UI code.
     * This prepares ImGui for a new frame and polls input events.
     */
    virtual void newFrame() = 0;

    /**
     * @brief Render ImGui draw data to command encoder
     * @param encoder RHI command encoder to record rendering commands
     * @param imageIndex Current swapchain image index
     *
     * This method should:
     * 1. Finalize ImGui draw data (ImGui::Render())
     * 2. Record rendering commands to the encoder
     * 3. Handle dynamic rendering or render pass as needed
     */
    virtual void render(rhi::RHICommandEncoder* encoder,
                       uint32_t imageIndex) = 0;

    /**
     * @brief Handle window resize event
     *
     * Called when the application window is resized.
     * Backend should recreate or adjust resources as needed.
     */
    virtual void handleResize() = 0;

    /**
     * @brief Shutdown and cleanup ImGui backend
     *
     * This method should:
     * 1. Destroy GPU resources (descriptor pools, textures, etc.)
     * 2. Shutdown ImGui renderer backend
     * 3. Shutdown ImGui platform backend (GLFW)
     * 4. Destroy ImGui context
     */
    virtual void shutdown() = 0;
};

} // namespace ui
