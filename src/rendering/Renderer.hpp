#pragma once

#include "src/core/VulkanDevice.hpp"
#include "src/rendering/VulkanSwapchain.hpp"
#include "src/rendering/VulkanPipeline.hpp"
#include "src/core/CommandManager.hpp"
#include "src/rendering/SyncManager.hpp"
#include "src/resources/VulkanImage.hpp"
#include "src/resources/VulkanBuffer.hpp"
#include "src/resources/ResourceManager.hpp"
#include "src/scene/SceneManager.hpp"
#include "src/utils/VulkanCommon.hpp"
#include "src/utils/Vertex.hpp"

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

    ~Renderer() = default;

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
     * @brief Draw a single frame
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

private:
    // Window reference
    GLFWwindow* window;

    // Core device
    std::unique_ptr<VulkanDevice> device;

    // Rendering components (directly owned)
    std::unique_ptr<VulkanSwapchain> swapchain;
    std::unique_ptr<VulkanPipeline> pipeline;
    std::unique_ptr<CommandManager> commandManager;
    std::unique_ptr<SyncManager> syncManager;

    // High-level managers
    std::unique_ptr<ResourceManager> resourceManager;
    std::unique_ptr<SceneManager> sceneManager;

    // Shared resources managed by Renderer
    std::unique_ptr<VulkanImage> depthImage;
    std::vector<std::unique_ptr<VulkanBuffer>> uniformBuffers;

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

    // Private initialization methods
    void createDepthResources();
    void createUniformBuffers();
    void createDescriptorPool();
    void createDescriptorSets();
    void updateDescriptorSets();

    // Rendering methods
    void recordCommandBuffer(uint32_t imageIndex);
    void updateUniformBuffer(uint32_t currentImage);
    void transitionImageLayout(
        uint32_t imageIndex,
        vk::ImageLayout oldLayout,
        vk::ImageLayout newLayout,
        vk::AccessFlags2 srcAccessMask,
        vk::AccessFlags2 dstAccessMask,
        vk::PipelineStageFlags2 srcStageMask,
        vk::PipelineStageFlags2 dstStageMask);

    // Swapchain recreation
    void recreateSwapchain();

    // Utility
    vk::Format findDepthFormat();
};
