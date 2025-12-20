#pragma once

#include "src/core/VulkanDevice.hpp"
#include "src/rendering/VulkanSwapchain.hpp"
#include "src/rendering/VulkanPipeline.hpp"
// Phase 7: CommandManager removed - using RHI command encoding
#include "src/rendering/SyncManager.hpp"
#include "src/resources/VulkanImage.hpp"
#include "src/resources/VulkanBuffer.hpp"
#include "src/resources/ResourceManager.hpp"
#include "src/scene/SceneManager.hpp"
#include "src/utils/VulkanCommon.hpp"
#include "src/utils/Vertex.hpp"
#include "src/rendering/RendererBridge.hpp"

#include <GLFW/glfw3.h>
#include <memory>
#include <vector>
#include <string>
#include <chrono>

/**
 * @brief High-level renderer coordinating subsystems (4-layer architecture)
 *
 * Responsibilities:
 * - Coordinate rendering components (swapchain, pipeline, command, sync)
 * - Coordinate ResourceManager and SceneManager
 * - Descriptor set management (shared across subsystems)
 * - Uniform buffer management
 * - Frame rendering orchestration
 *
 * Does NOT:
 * - Know about file I/O (encapsulated in ResourceManager)
 * - Know about OBJ parsing (encapsulated in SceneManager)
 * - Handle low-level staging buffers (delegated to ResourceManager)
 */
class Renderer {
public:
    /**
     * @brief Construct renderer with window
     * @param window GLFW window for surface creation
     * @param validationLayers Validation layers to enable
     * @param enableValidation Whether to enable validation
     * @param useFdfMode Use FDF wireframe mode (default: false)
     */
    Renderer(GLFWwindow* window,
             const std::vector<const char*>& validationLayers,
             bool enableValidation,
             bool useFdfMode = false);

    ~Renderer();

    // Disable copy and move
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    /**
     * @brief Load model from file
     * @param modelPath Path to model file
     */
    void loadModel(const std::string& modelPath);

    /**
     * @brief Load texture from file
     * @param texturePath Path to texture file
     */
    void loadTexture(const std::string& texturePath);

    /**
     * @brief Draw a single frame using RHI rendering (Phase 7: Full RHI migration)
     */
    void drawFrame();

    /**
     * @brief Wait for device to be idle (for cleanup)
     */
    void waitIdle();

    /**
     * @brief Handle framebuffer resize
     */
    void handleFramebufferResize();

    /**
     * @brief Update camera matrices
     * @param view View matrix
     * @param projection Projection matrix
     */
    void updateCamera(const glm::mat4& view, const glm::mat4& projection);

    /**
     * @brief Check if FDF mode is active
     */
    bool isFdfMode() const { return fdfMode; }

    /**
     * @brief Adjust Z-scale for FDF visualization
     * @param delta Change in Z-scale (can be positive or negative)
     */
    void adjustZScale(float delta);

    /**
     * @brief Get current Z-scale value
     */
    float getZScale() const { return zScale; }

    /**
     * @brief Get mesh bounding box center (for camera targeting)
     * @return Center of primary mesh's bounding box
     */
    glm::vec3 getMeshCenter() const;

    /**
     * @brief Get mesh bounding box radius (for camera far plane calculation)
     * @return Radius of primary mesh's bounding sphere
     */
    float getMeshRadius() const;

    /**
     * @brief Get Vulkan device (for external components like ImGui)
     */
    VulkanDevice& getDevice() { return *device; }

    /**
     * @brief Get swapchain (for external components like ImGui)
     */
    VulkanSwapchain& getSwapchain() { return *swapchain; }

    /**
     * @brief Get RHI device (for external components like ImGui)
     */
    rhi::RHIDevice* getRHIDevice() { return rhiBridge ? rhiBridge->getDevice() : nullptr; }

    /**
     * @brief Get RHI swapchain (for external components like ImGui)
     */
    rhi::RHISwapchain* getRHISwapchain() { return rhiBridge ? rhiBridge->getSwapchain() : nullptr; }

    /**
     * @brief Get ImGui manager (for external UI updates)
     */
    class ImGuiManager* getImGuiManager() { return imguiManager.get(); }

    /**
     * @brief Initialize ImGui subsystem
     */
    void initImGui(GLFWwindow* window);


private:
    // Window reference
    GLFWwindow* window;

    // RHI Bridge (Phase 4 - provides RHI device access)
    std::unique_ptr<rendering::RendererBridge> rhiBridge;

    // Core device
    std::unique_ptr<VulkanDevice> device;

    // Rendering components (directly owned)
    std::unique_ptr<VulkanSwapchain> swapchain;
    std::unique_ptr<VulkanPipeline> pipeline;
    // Phase 7: CommandManager removed - using RHI command encoding
    std::unique_ptr<SyncManager> syncManager;

    // High-level managers
    std::unique_ptr<ResourceManager> resourceManager;
    std::unique_ptr<SceneManager> sceneManager;
    std::unique_ptr<class ImGuiManager> imguiManager;  // Phase 6: ImGui integration

    // Shared resources managed by Renderer
    std::unique_ptr<VulkanImage> depthImage;
    std::vector<std::unique_ptr<VulkanBuffer>> uniformBuffers;

    // RHI resources (Phase 4 migration - parallel to legacy resources)
    std::unique_ptr<rhi::RHITexture> rhiDepthImage;
    std::unique_ptr<rhi::RHITextureView> rhiDepthImageView;  // Cached depth view
    std::vector<std::unique_ptr<rhi::RHIBuffer>> rhiUniformBuffers;
    std::unique_ptr<rhi::RHIBindGroupLayout> rhiBindGroupLayout;
    std::vector<std::unique_ptr<rhi::RHIBindGroup>> rhiBindGroups;

    // RHI Pipeline (Phase 4.4)
    std::unique_ptr<rhi::RHIShader> rhiVertexShader;
    std::unique_ptr<rhi::RHIShader> rhiFragmentShader;
    std::unique_ptr<rhi::RHIPipelineLayout> rhiPipelineLayout;
    std::unique_ptr<rhi::RHIRenderPipeline> rhiPipeline;

    // RHI Vertex/Index Buffers (Phase 4.5)
    std::unique_ptr<rhi::RHIBuffer> rhiVertexBuffer;
    std::unique_ptr<rhi::RHIBuffer> rhiIndexBuffer;
    uint32_t rhiIndexCount = 0;

    // Descriptor management
    vk::raii::DescriptorPool descriptorPool = nullptr;
    std::vector<vk::raii::DescriptorSet> descriptorSets;

    // Frame synchronization
    uint32_t currentFrame = 0;
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    // For uniform buffer animation
    std::chrono::time_point<std::chrono::high_resolution_clock> startTime;

    // Camera matrices
    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;

    // Mode flag
    bool fdfMode;

    // FDF Z-scale factor
    float zScale = 1.0f;
    std::string currentModelPath;

    // Private initialization methods
    void createDepthResources();
    void createUniformBuffers();
    void createDescriptorPool();
    void createDescriptorSets();
    void updateDescriptorSets();

    // RHI initialization methods (Phase 4)
    void createRHIDepthResources();
    void createRHIUniformBuffers();
    void createRHIBindGroups();
    void createRHIPipeline();  // Phase 4.4
    void createRHIBuffers();   // Phase 4.5 - vertex/index buffers

    // RHI command recording (Phase 4.2)
    void recordRHICommandBuffer(uint32_t imageIndex);
    void updateRHIUniformBuffer(uint32_t currentImage);

    // Phase 7: Legacy rendering methods removed (drawFrameLegacy, recordCommandBuffer, updateUniformBuffer, transitionImageLayout)
    // Now using RHI-based rendering via drawFrame()

    // Swapchain recreation
    void recreateSwapchain();

    // Utility
    vk::Format findDepthFormat();
};
