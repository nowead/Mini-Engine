#include "VulkanRHIDevice.hpp"
#include "VulkanRHIQueue.hpp"
#include "VulkanRHIBuffer.hpp"
#include "VulkanRHITexture.hpp"
#include "VulkanRHICapabilities.hpp"
#include "VulkanRHISampler.hpp"
#include "VulkanRHIShader.hpp"
#include "VulkanRHISync.hpp"
#include "VulkanRHIBindGroup.hpp"
#include "VulkanRHIPipeline.hpp"
#include "VulkanRHICommandEncoder.hpp"
#include "VulkanRHISwapchain.hpp"
#include <iostream>
#include <set>

namespace RHI {
namespace Vulkan {

// ============================================================================
// Constructor / Destructor
// ============================================================================

VulkanRHIDevice::VulkanRHIDevice(GLFWwindow* window, bool enableValidation)
    : m_enableValidationLayers(enableValidation)
{
    createInstance(enableValidation);

    if (enableValidation) {
        setupDebugMessenger();
    }

    createSurface(window);
    pickPhysicalDevice();
    createLogicalDevice();
    createVmaAllocator();
    createCommandPool();
    createDescriptorPool();
    queryCapabilities();

    // Create RHI queue wrapper
    m_rhiGraphicsQueue = std::make_unique<VulkanRHIQueue>(
        this, m_graphicsQueue, m_graphicsQueueFamily, QueueType::Graphics);
}

VulkanRHIDevice::~VulkanRHIDevice() {
    // Wait for device to be idle before cleanup
    if (*m_device != VK_NULL_HANDLE) {
        m_device.waitIdle();
    }

    // Destroy VMA allocator
    if (m_vmaAllocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_vmaAllocator);
        m_vmaAllocator = VK_NULL_HANDLE;
    }

    // RAII objects clean up automatically in reverse order
}

// ============================================================================
// Initialization Methods
// ============================================================================

void VulkanRHIDevice::createInstance(bool enableValidation) {
    vk::ApplicationInfo appInfo{
        .pApplicationName = "Mini-Engine",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "Mini-Engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_3
    };

    auto extensions = getRequiredExtensions(enableValidation);

    vk::InstanceCreateInfo createInfo{
        .pApplicationInfo = &appInfo,
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data()
    };

    if (enableValidation) {
        if (!checkValidationLayerSupport()) {
            throw std::runtime_error("Validation layers requested but not available!");
        }

        createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        createInfo.ppEnabledLayerNames = m_validationLayers.data();
    }

    m_instance = vk::raii::Instance(m_context, createInfo);
}

void VulkanRHIDevice::setupDebugMessenger() {
    vk::DebugUtilsMessengerCreateInfoEXT createInfo;
    createInfo.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
                                 vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                                 vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
    createInfo.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                            vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                            vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
    // Need to cast the C API callback to C++ API type - they're ABI compatible
    createInfo.pfnUserCallback = reinterpret_cast<vk::PFN_DebugUtilsMessengerCallbackEXT>(
        static_cast<PFN_vkDebugUtilsMessengerCallbackEXT>(debugCallback));

    m_debugMessenger = vk::raii::DebugUtilsMessengerEXT(m_instance, createInfo);
}

void VulkanRHIDevice::createSurface(GLFWwindow* window) {
    VkSurfaceKHR surface;
    VkResult result = glfwCreateWindowSurface(*m_instance, window, nullptr, &surface);
    CheckVkResult(result, "glfwCreateWindowSurface");

    m_surface = vk::raii::SurfaceKHR(m_instance, surface);
}

void VulkanRHIDevice::pickPhysicalDevice() {
    auto devices = m_instance.enumeratePhysicalDevices();

    if (devices.empty()) {
        throw std::runtime_error("Failed to find GPUs with Vulkan support!");
    }

    // Pick the first discrete GPU, or fallback to first device
    for (const auto& device : devices) {
        auto properties = device.getProperties();
        if (properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
            m_physicalDevice = device;
            std::cout << "Selected GPU: " << properties.deviceName << std::endl;
            return;
        }
    }

    // Fallback to first device
    m_physicalDevice = devices[0];
    auto properties = m_physicalDevice.getProperties();
    std::cout << "Selected GPU: " << properties.deviceName << std::endl;
}

void VulkanRHIDevice::createLogicalDevice() {
    // Find graphics queue family
    auto queueFamilies = m_physicalDevice.getQueueFamilyProperties();

    for (uint32_t i = 0; i < queueFamilies.size(); i++) {
        if (queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics) {
            // Check if this queue family supports present
            if (m_physicalDevice.getSurfaceSupportKHR(i, *m_surface)) {
                m_graphicsQueueFamily = i;
                break;
            }
        }
    }

    if (m_graphicsQueueFamily == ~0u) {
        throw std::runtime_error("Failed to find suitable queue family!");
    }

    // Queue create info
    float queuePriority = 1.0f;
    vk::DeviceQueueCreateInfo queueCreateInfo{
        .queueFamilyIndex = m_graphicsQueueFamily,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority
    };

    // Device features
    vk::PhysicalDeviceFeatures deviceFeatures{
        .fillModeNonSolid = VK_TRUE,
        .samplerAnisotropy = VK_TRUE
    };

    // Device create info
    vk::DeviceCreateInfo createInfo{
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCreateInfo,
        .enabledExtensionCount = static_cast<uint32_t>(m_deviceExtensions.size()),
        .ppEnabledExtensionNames = m_deviceExtensions.data(),
        .pEnabledFeatures = &deviceFeatures
    };

    if (m_enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        createInfo.ppEnabledLayerNames = m_validationLayers.data();
    }

    m_device = vk::raii::Device(m_physicalDevice, createInfo);
    m_graphicsQueue = vk::raii::Queue(m_device, m_graphicsQueueFamily, 0);
}

void VulkanRHIDevice::createVmaAllocator() {
    VmaVulkanFunctions vulkanFunctions{};
    vulkanFunctions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.physicalDevice = *m_physicalDevice;
    allocatorInfo.device = *m_device;
    allocatorInfo.instance = *m_instance;
    allocatorInfo.pVulkanFunctions = &vulkanFunctions;

    VkResult result = vmaCreateAllocator(&allocatorInfo, &m_vmaAllocator);
    CheckVkResult(result, "vmaCreateAllocator");
}

void VulkanRHIDevice::createCommandPool() {
    vk::CommandPoolCreateInfo poolInfo;
    poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    poolInfo.queueFamilyIndex = m_graphicsQueueFamily;

    m_commandPool = vk::raii::CommandPool(m_device, poolInfo);
}

void VulkanRHIDevice::createDescriptorPool() {
    // Create a large descriptor pool for bind groups
    // TODO: Implement per-frame pools with automatic reset for better performance
    std::vector<vk::DescriptorPoolSize> poolSizes = {
        { vk::DescriptorType::eUniformBuffer, 1000 },
        { vk::DescriptorType::eUniformBufferDynamic, 1000 },
        { vk::DescriptorType::eStorageBuffer, 1000 },
        { vk::DescriptorType::eStorageBufferDynamic, 1000 },
        { vk::DescriptorType::eSampler, 1000 },
        { vk::DescriptorType::eSampledImage, 1000 },
        { vk::DescriptorType::eStorageImage, 1000 }
    };

    vk::DescriptorPoolCreateInfo poolInfo;
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;  // Allow freeing individual sets
    poolInfo.maxSets = 1000;  // Max number of descriptor sets that can be allocated
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    m_descriptorPool = vk::raii::DescriptorPool(m_device, poolInfo);
}

void VulkanRHIDevice::queryCapabilities() {
    m_capabilities = std::make_unique<VulkanRHICapabilities>(m_physicalDevice);
}

// ============================================================================
// RHIDevice Interface Implementation
// ============================================================================

const RHICapabilities& VulkanRHIDevice::getCapabilities() const {
    return *m_capabilities;
}

RHIQueue* VulkanRHIDevice::getQueue(QueueType type) {
    // Currently only graphics queue is supported
    if (type == QueueType::Graphics) {
        return m_rhiGraphicsQueue.get();
    }

    // TODO: Support compute and transfer queues
    return nullptr;
}

std::unique_ptr<RHIBuffer> VulkanRHIDevice::createBuffer(const BufferDesc& desc) {
    return std::make_unique<VulkanRHIBuffer>(this, desc);
}

std::unique_ptr<RHITexture> VulkanRHIDevice::createTexture(const TextureDesc& desc) {
    return std::make_unique<VulkanRHITexture>(this, desc);
}

std::unique_ptr<RHISampler> VulkanRHIDevice::createSampler(const SamplerDesc& desc) {
    return std::make_unique<VulkanRHISampler>(this, desc);
}

std::unique_ptr<RHIShader> VulkanRHIDevice::createShader(const ShaderDesc& desc) {
    return std::make_unique<VulkanRHIShader>(this, desc);
}

std::unique_ptr<RHIBindGroupLayout> VulkanRHIDevice::createBindGroupLayout(
    const BindGroupLayoutDesc& desc)
{
    return std::make_unique<VulkanRHIBindGroupLayout>(this, desc);
}

std::unique_ptr<RHIBindGroup> VulkanRHIDevice::createBindGroup(const BindGroupDesc& desc) {
    return std::make_unique<VulkanRHIBindGroup>(this, desc);
}

std::unique_ptr<RHIPipelineLayout> VulkanRHIDevice::createPipelineLayout(
    const PipelineLayoutDesc& desc)
{
    return std::make_unique<VulkanRHIPipelineLayout>(this, desc);
}

std::unique_ptr<RHIRenderPipeline> VulkanRHIDevice::createRenderPipeline(
    const RenderPipelineDesc& desc)
{
    return std::make_unique<VulkanRHIRenderPipeline>(this, desc);
}

std::unique_ptr<RHIComputePipeline> VulkanRHIDevice::createComputePipeline(
    const ComputePipelineDesc& desc)
{
    return std::make_unique<VulkanRHIComputePipeline>(this, desc);
}

std::unique_ptr<RHICommandEncoder> VulkanRHIDevice::createCommandEncoder() {
    return std::make_unique<VulkanRHICommandEncoder>(this);
}

std::unique_ptr<RHISwapchain> VulkanRHIDevice::createSwapchain(const SwapchainDesc& desc) {
    return std::make_unique<VulkanRHISwapchain>(this, desc);
}

std::unique_ptr<RHIFence> VulkanRHIDevice::createFence(bool signaled) {
    return std::make_unique<VulkanRHIFence>(this, signaled);
}

std::unique_ptr<RHISemaphore> VulkanRHIDevice::createSemaphore() {
    return std::make_unique<VulkanRHISemaphore>(this);
}

void VulkanRHIDevice::waitIdle() {
    m_device.waitIdle();
}

// ============================================================================
// Helper Methods
// ============================================================================

std::vector<const char*> VulkanRHIDevice::getRequiredExtensions(bool enableValidation) const {
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (enableValidation) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    // macOS requires portability enumeration
#ifdef __APPLE__
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#endif

    return extensions;
}

bool VulkanRHIDevice::checkValidationLayerSupport() const {
    auto availableLayers = m_context.enumerateInstanceLayerProperties();

    for (const char* layerName : m_validationLayers) {
        bool layerFound = false;

        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) {
            return false;
        }
    }

    return true;
}

uint32_t VulkanRHIDevice::findMemoryType(uint32_t typeFilter,
                                         vk::MemoryPropertyFlags properties) const {
    auto memProperties = m_physicalDevice.getMemoryProperties();

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memory type!");
}

// ============================================================================
// Debug Callback
// ============================================================================

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanRHIDevice::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << "[Vulkan] " << pCallbackData->pMessage << std::endl;
    }

    return VK_FALSE;
}

} // namespace Vulkan
} // namespace RHI
