#pragma once

#include "VulkanCommon.hpp"
#include <GLFW/glfw3.h>

namespace RHI {
namespace Vulkan {

// Bring RHI types into scope
using rhi::RHIDevice;
using rhi::RHIBackendType;
using rhi::RHICapabilities;
using rhi::RHIQueue;
using rhi::QueueType;
using rhi::RHIBuffer;
using rhi::BufferDesc;
using rhi::RHITexture;
using rhi::TextureDesc;
using rhi::RHISampler;
using rhi::SamplerDesc;
using rhi::RHIShader;
using rhi::ShaderDesc;
using rhi::RHIBindGroupLayout;
using rhi::BindGroupLayoutDesc;
using rhi::RHIBindGroup;
using rhi::BindGroupDesc;
using rhi::RHIPipelineLayout;
using rhi::PipelineLayoutDesc;
using rhi::RHIRenderPipeline;
using rhi::RenderPipelineDesc;
using rhi::RHIComputePipeline;
using rhi::ComputePipelineDesc;
using rhi::RHICommandEncoder;
using rhi::RHISwapchain;
using rhi::SwapchainDesc;
using rhi::RHIFence;
using rhi::RHISemaphore;

/**
 * @brief Vulkan implementation of RHIDevice
 *
 * This is the main device interface for the Vulkan backend.
 * It wraps the existing VulkanDevice and provides RHI-compliant factory methods.
 */
class VulkanRHIDevice : public RHIDevice {
public:
    /**
     * @brief Create Vulkan RHI device
     * @param window GLFW window for surface creation
     * @param enableValidation Enable Vulkan validation layers
     */
    VulkanRHIDevice(GLFWwindow* window, bool enableValidation = true);
    ~VulkanRHIDevice() override;

    // Non-copyable, movable
    VulkanRHIDevice(const VulkanRHIDevice&) = delete;
    VulkanRHIDevice& operator=(const VulkanRHIDevice&) = delete;
    VulkanRHIDevice(VulkanRHIDevice&&) = default;
    VulkanRHIDevice& operator=(VulkanRHIDevice&&) = default;

    // RHIDevice interface implementation
    RHIBackendType getBackendType() const override { return RHIBackendType::Vulkan; }

    const RHICapabilities& getCapabilities() const override;

    RHIQueue* getQueue(QueueType type) override;

    std::unique_ptr<RHIBuffer> createBuffer(const BufferDesc& desc) override;
    std::unique_ptr<RHITexture> createTexture(const TextureDesc& desc) override;
    std::unique_ptr<RHISampler> createSampler(const SamplerDesc& desc) override;
    std::unique_ptr<RHIShader> createShader(const ShaderDesc& desc) override;
    std::unique_ptr<RHIBindGroupLayout> createBindGroupLayout(const BindGroupLayoutDesc& desc) override;
    std::unique_ptr<RHIBindGroup> createBindGroup(const BindGroupDesc& desc) override;
    std::unique_ptr<RHIPipelineLayout> createPipelineLayout(const PipelineLayoutDesc& desc) override;
    std::unique_ptr<RHIRenderPipeline> createRenderPipeline(const RenderPipelineDesc& desc) override;
    std::unique_ptr<RHIComputePipeline> createComputePipeline(const ComputePipelineDesc& desc) override;
    std::unique_ptr<RHICommandEncoder> createCommandEncoder() override;
    std::unique_ptr<RHISwapchain> createSwapchain(const SwapchainDesc& desc) override;
    std::unique_ptr<RHIFence> createFence(bool signaled = false) override;
    std::unique_ptr<RHISemaphore> createSemaphore() override;

    void waitIdle() override;

    // Vulkan-specific accessors (for internal use)
    vk::raii::Device& getVkDevice() { return m_device; }
    vk::raii::PhysicalDevice& getVkPhysicalDevice() { return m_physicalDevice; }
    vk::raii::Instance& getVkInstance() { return m_instance; }
    VmaAllocator getVmaAllocator() { return m_vmaAllocator; }
    vk::raii::Queue& getVkGraphicsQueue() { return m_graphicsQueue; }
    uint32_t getGraphicsQueueFamilyIndex() const { return m_graphicsQueueFamily; }
    vk::raii::SurfaceKHR& getVkSurface() { return m_surface; }
    vk::DescriptorPool getDescriptorPool() { return *m_descriptorPool; }
    vk::CommandPool getCommandPool() { return *m_commandPool; }

private:
    // Initialization methods
    void createInstance(bool enableValidation);
    void setupDebugMessenger();
    void createSurface(GLFWwindow* window);
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createVmaAllocator();
    void createCommandPool();
    void createDescriptorPool();
    void queryCapabilities();

    // Helper methods
    std::vector<const char*> getRequiredExtensions(bool enableValidation) const;
    bool checkValidationLayerSupport() const;
    uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const;

    // Vulkan objects
    vk::raii::Context m_context;
    vk::raii::Instance m_instance = nullptr;
    vk::raii::DebugUtilsMessengerEXT m_debugMessenger = nullptr;
    vk::raii::SurfaceKHR m_surface = nullptr;
    vk::raii::PhysicalDevice m_physicalDevice = nullptr;
    vk::raii::Device m_device = nullptr;

    // Queues
    vk::raii::Queue m_graphicsQueue = nullptr;
    uint32_t m_graphicsQueueFamily = ~0u;

    // VMA
    VmaAllocator m_vmaAllocator = VK_NULL_HANDLE;

    // Command pool for command buffers
    vk::raii::CommandPool m_commandPool = nullptr;

    // Descriptor pool for bind groups
    vk::raii::DescriptorPool m_descriptorPool = nullptr;

    // RHI objects
    std::unique_ptr<RHICapabilities> m_capabilities;
    std::unique_ptr<RHIQueue> m_rhiGraphicsQueue;

    // Configuration
    bool m_enableValidationLayers = false;
    std::vector<const char*> m_validationLayers = {"VK_LAYER_KHRONOS_validation"};
    std::vector<const char*> m_deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    // Debug callback
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);
};

} // namespace Vulkan
} // namespace RHI
