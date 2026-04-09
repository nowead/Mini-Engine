#include <rhi/vulkan/VulkanRHIDevice.hpp>
#include <rhi/vulkan/VulkanRHIQueue.hpp>
#include <rhi/vulkan/VulkanRHIBuffer.hpp>
#include <rhi/vulkan/VulkanRHITexture.hpp>
#include <rhi/vulkan/VulkanRHICapabilities.hpp>
#include <rhi/vulkan/VulkanRHISampler.hpp>
#include <rhi/vulkan/VulkanRHIShader.hpp>
#include <rhi/vulkan/VulkanRHISync.hpp>
#include <rhi/vulkan/VulkanRHIBindGroup.hpp>
#include <rhi/vulkan/VulkanRHIPipeline.hpp>
#include <rhi/vulkan/VulkanRHICommandEncoder.hpp>
#include <rhi/vulkan/VulkanRHISwapchain.hpp>
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
    createComputeCommandPool();
    createDescriptorPool();
    createStagingPool();
    createPerFrameDescriptorPools();
    queryCapabilities();

    // Create RHI queue wrappers
    m_rhiGraphicsQueue = std::make_unique<VulkanRHIQueue>(
        this, m_graphicsQueue, m_graphicsQueueFamily, QueueType::Graphics);
    if (m_hasDedicatedComputeQueue) {
        m_rhiComputeQueue = std::make_unique<VulkanRHIQueue>(
            this, m_computeQueue, m_computeQueueFamily, QueueType::Compute);
    }
}

VulkanRHIDevice::~VulkanRHIDevice() {
    // Wait for device to be idle before cleanup
    if (*m_device != VK_NULL_HANDLE) {
        m_device.waitIdle();
    }

    // Phase 4: Destroy VMA staging pool before allocator
    if (m_stagingPool != VK_NULL_HANDLE) {
        vmaDestroyPool(m_vmaAllocator, m_stagingPool);
        m_stagingPool = VK_NULL_HANDLE;
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
#ifdef __APPLE__
        // macOS requires this flag for MoltenVK portability
        .flags = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR,
#endif
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
    std::cout << "Creating logical device..." << std::endl;

    // Find queue families
    auto queueFamilies = m_physicalDevice.getQueueFamilyProperties();
    std::cout << "Found " << queueFamilies.size() << " queue families" << std::endl;

    // Find graphics queue family
    for (uint32_t i = 0; i < queueFamilies.size(); i++) {
        if (queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics) {
            if (m_physicalDevice.getSurfaceSupportKHR(i, *m_surface)) {
                m_graphicsQueueFamily = i;
                std::cout << "Using queue family " << i << " for graphics and present" << std::endl;
                break;
            }
        }
    }

    if (m_graphicsQueueFamily == ~0u) {
        throw std::runtime_error("Failed to find suitable queue family!");
    }

    // Find dedicated compute queue family (has Compute but not Graphics)
    for (uint32_t i = 0; i < queueFamilies.size(); i++) {
        if ((queueFamilies[i].queueFlags & vk::QueueFlagBits::eCompute) &&
            !(queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics)) {
            m_computeQueueFamily = i;
            m_hasDedicatedComputeQueue = true;
            std::cout << "Dedicated compute queue family: " << i << std::endl;
            break;
        }
    }

    // Fallback: use graphics queue family for compute
    if (m_computeQueueFamily == ~0u) {
        m_computeQueueFamily = m_graphicsQueueFamily;
        m_hasDedicatedComputeQueue = false;
        std::cout << "No dedicated compute queue, using graphics queue fallback" << std::endl;
    }

    // Queue create infos for unique families
    float queuePriority = 1.0f;
    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = { m_graphicsQueueFamily, m_computeQueueFamily };
    for (uint32_t family : uniqueQueueFamilies) {
        queueCreateInfos.push_back(vk::DeviceQueueCreateInfo{
            .queueFamilyIndex = family,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority
        });
    }

    // Device features
    std::cout << "Querying device features..." << std::endl;
    auto availableFeatures = m_physicalDevice.getFeatures();
    vk::PhysicalDeviceFeatures deviceFeatures{
        .fillModeNonSolid = availableFeatures.fillModeNonSolid,
        .samplerAnisotropy = availableFeatures.samplerAnisotropy
    };
    std::cout << "fillModeNonSolid: " << availableFeatures.fillModeNonSolid << ", samplerAnisotropy: " << availableFeatures.samplerAnisotropy << std::endl;

    // Query timeline semaphore support
    auto featureChain = m_physicalDevice.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan12Features>();
    auto& features12 = featureChain.get<vk::PhysicalDeviceVulkan12Features>();
    m_hasTimelineSemaphores = features12.timelineSemaphore;
    std::cout << "Timeline semaphores: " << (m_hasTimelineSemaphores ? "supported" : "not supported") << std::endl;

    // Phase 4: Query descriptor indexing (bindless) support via extension properties
    // Promoted to Vulkan 1.2 core; also available as VK_EXT_descriptor_indexing on 1.1 devices.
    {
        auto extProps = m_physicalDevice.enumerateDeviceExtensionProperties();
        bool hasDescIndexingExt = false;
        for (const auto& ext : extProps) {
            if (std::string_view(ext.extensionName) == VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME) {
                hasDescIndexingExt = true;
                break;
            }
        }
        if (hasDescIndexingExt) {
            // Query the actual feature bits
            auto descChain = m_physicalDevice.getFeatures2<
                vk::PhysicalDeviceFeatures2,
                vk::PhysicalDeviceDescriptorIndexingFeatures>();
            auto& di = descChain.get<vk::PhysicalDeviceDescriptorIndexingFeatures>();
            m_hasDescriptorIndexing =
                di.descriptorBindingPartiallyBound &&
                di.descriptorBindingSampledImageUpdateAfterBind &&
                di.runtimeDescriptorArray &&
                di.shaderSampledImageArrayNonUniformIndexing;
            if (m_hasDescriptorIndexing) {
                m_deviceExtensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
                std::cout << "[VulkanRHIDevice] Descriptor indexing (bindless) supported\n";
            }
        }
        if (!m_hasDescriptorIndexing) {
            std::cout << "[VulkanRHIDevice] Descriptor indexing not available — bindless disabled\n";
        }
    }

    // Build pNext chain: dynamicRendering -> sync2 -> (optional) timelineSemaphore
    //                 -> (optional) descriptorIndexing
    vk::PhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures{
        .dynamicRendering = VK_TRUE
    };
    vk::PhysicalDeviceSynchronization2Features sync2Features{
        .pNext = &dynamicRenderingFeatures,
        .synchronization2 = VK_TRUE
    };
    vk::PhysicalDeviceTimelineSemaphoreFeatures timelineSemaphoreFeatures{
        .pNext = &sync2Features,
        .timelineSemaphore = m_hasTimelineSemaphores ? VK_TRUE : VK_FALSE
    };
    // Phase 4: descriptor indexing features appended to the chain when supported
    // Field order matches the struct definition in VkPhysicalDeviceDescriptorIndexingFeatures
    vk::PhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeatures{};
    descriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing  = VK_TRUE;
    descriptorIndexingFeatures.descriptorBindingPartiallyBound            = VK_TRUE;
    descriptorIndexingFeatures.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    descriptorIndexingFeatures.runtimeDescriptorArray                     = VK_TRUE;

    // Assemble the chain head
    void* featureChainHead;
    if (m_hasDescriptorIndexing) {
        // timelineSemaphore -> sync2 -> dynamicRendering (already chained above)
        // Add descriptorIndexing at the front
        descriptorIndexingFeatures.pNext = m_hasTimelineSemaphores
            ? static_cast<void*>(&timelineSemaphoreFeatures)
            : static_cast<void*>(&sync2Features);
        featureChainHead = &descriptorIndexingFeatures;
    } else if (m_hasTimelineSemaphores) {
        featureChainHead = &timelineSemaphoreFeatures;
    } else {
        featureChainHead = &sync2Features;
    }

    vk::PhysicalDeviceFeatures2 deviceFeatures2{
        .pNext = featureChainHead,
        .features = deviceFeatures
    };

    vk::DeviceCreateInfo createInfo{
        .pNext = &deviceFeatures2,
        .queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size()),
        .pQueueCreateInfos = queueCreateInfos.data(),
        .enabledExtensionCount = static_cast<uint32_t>(m_deviceExtensions.size()),
        .ppEnabledExtensionNames = m_deviceExtensions.data()
    };

#ifdef __APPLE__
    std::cout << "Device extensions: ";
    for (const auto& ext : m_deviceExtensions) {
        std::cout << ext << " ";
    }
    std::cout << std::endl;
#endif

    if (m_enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        createInfo.ppEnabledLayerNames = m_validationLayers.data();
    }

    std::cout << "Creating vk::raii::Device..." << std::endl;
    m_device = vk::raii::Device(m_physicalDevice, createInfo);
    std::cout << "Device created, getting queues..." << std::endl;
    m_graphicsQueue = vk::raii::Queue(m_device, m_graphicsQueueFamily, 0);
    m_computeQueue = vk::raii::Queue(m_device, m_computeQueueFamily, 0);
    std::cout << "Logical device creation complete" << std::endl;
}

void VulkanRHIDevice::createVmaAllocator() {
    std::cout << "Creating VMA allocator..." << std::endl;

    // Use the physical device's actual API version so VMA doesn't try to call
    // Vulkan 1.2/1.3 functions that may not be available on all drivers (e.g. lavapipe).
    auto physProps = m_physicalDevice.getProperties();
    uint32_t actualApiVersion = physProps.apiVersion;
    // Cap at 1.3 — we don't need anything newer
    if (actualApiVersion > VK_API_VERSION_1_3) actualApiVersion = VK_API_VERSION_1_3;

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.vulkanApiVersion = actualApiVersion;
    allocatorInfo.physicalDevice = *m_physicalDevice;
    allocatorInfo.device = *m_device;
    allocatorInfo.instance = *m_instance;
    // Using VMA_STATIC_VULKAN_FUNCTIONS=1, so no need to provide pVulkanFunctions

    VkResult result = vmaCreateAllocator(&allocatorInfo, &m_vmaAllocator);
    CheckVkResult(result, "vmaCreateAllocator");
    std::cout << "VMA allocator created successfully" << std::endl;
}

void VulkanRHIDevice::createCommandPool() {
    vk::CommandPoolCreateInfo poolInfo;
    poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    poolInfo.queueFamilyIndex = m_graphicsQueueFamily;

    m_commandPool = vk::raii::CommandPool(m_device, poolInfo);
}

void VulkanRHIDevice::createComputeCommandPool() {
    if (m_hasDedicatedComputeQueue) {
        vk::CommandPoolCreateInfo poolInfo;
        poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        poolInfo.queueFamilyIndex = m_computeQueueFamily;

        m_computeCommandPool = vk::raii::CommandPool(m_device, poolInfo);
    }
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

void VulkanRHIDevice::createStagingPool() {
    // Phase 4: Linear-algorithm VMA pool for short-lived upload (CopySrc|MapWrite) buffers.
    // LINEAR_ALGORITHM allocates sequentially and frees all at once — ideal for
    // frame-scoped staging buffers that are created, used for upload, then destroyed.
    VmaPoolCreateInfo poolInfo{};
    poolInfo.memoryTypeIndex = 0;  // Will be resolved by VMA
    poolInfo.flags = VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT;
    poolInfo.blockSize = 16 * 1024 * 1024;  // 16 MB blocks
    poolInfo.minBlockCount = 1;
    poolInfo.maxBlockCount = 8;

    // Resolve the memory type index for HOST_VISIBLE | HOST_COHERENT memory
    VkMemoryRequirements memReq{};
    memReq.size = 1;
    memReq.alignment = 1;
    memReq.memoryTypeBits = 0xFFFFFFFF;

    VmaAllocationCreateInfo testAllocInfo{};
    testAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    testAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VkBufferCreateInfo testBufInfo{};
    testBufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    testBufInfo.size  = 64;
    testBufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    uint32_t memTypeIdx = 0;
    VkResult res = vmaFindMemoryTypeIndexForBufferInfo(
        m_vmaAllocator,
        &testBufInfo,
        &testAllocInfo,
        &memTypeIdx);

    if (res == VK_SUCCESS) {
        poolInfo.memoryTypeIndex = memTypeIdx;
        VkResult poolRes = vmaCreatePool(m_vmaAllocator, &poolInfo, &m_stagingPool);
        if (poolRes != VK_SUCCESS) {
            std::cerr << "[VulkanRHIDevice] Warning: failed to create staging pool (VMA error "
                      << poolRes << "), falling back to default allocator\n";
            m_stagingPool = VK_NULL_HANDLE;
        } else {
            std::cout << "[VulkanRHIDevice] VMA staging pool created (16MB blocks, linear algorithm)\n";
        }
    } else {
        std::cerr << "[VulkanRHIDevice] Warning: could not resolve staging memory type, skipping staging pool\n";
    }
}

void VulkanRHIDevice::createPerFrameDescriptorPools() {
    // Phase 4: Two descriptor pools (one per frame-in-flight) for dynamic bind groups
    // that are created and destroyed each frame. Reset via resetPerFrameDescriptorPool().
    // Currently serves as infrastructure; future dynamic bind group allocations use these pools.
    std::vector<vk::DescriptorPoolSize> poolSizes = {
        { vk::DescriptorType::eUniformBuffer,        256 },
        { vk::DescriptorType::eStorageBuffer,        256 },
        { vk::DescriptorType::eSampler,              128 },
        { vk::DescriptorType::eSampledImage,         256 },
        { vk::DescriptorType::eStorageImage,          64 },
        { vk::DescriptorType::eCombinedImageSampler, 256 },
    };

    vk::DescriptorPoolCreateInfo poolInfo;
    // NOTE: no eFreeDescriptorSet — entire pool is reset at once each frame
    poolInfo.maxSets = 256;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    for (int i = 0; i < 2; ++i) {
        m_perFrameDescriptorPools[i] = vk::raii::DescriptorPool(m_device, poolInfo);
    }
    std::cout << "[VulkanRHIDevice] Per-frame descriptor pools created (2x256 sets)\n";
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

const std::string& VulkanRHIDevice::getDeviceName() const {
    static std::string deviceName;
    if (deviceName.empty() && *m_physicalDevice) {
        auto props = m_physicalDevice.getProperties();
        deviceName = std::string(props.deviceName.data());
    }
    return deviceName;
}

RHIQueue* VulkanRHIDevice::getQueue(QueueType type) {
    if (type == QueueType::Graphics) {
        return m_rhiGraphicsQueue.get();
    }
    if (type == QueueType::Compute) {
        // Return dedicated compute queue if available, otherwise fallback to graphics
        return m_rhiComputeQueue ? m_rhiComputeQueue.get() : m_rhiGraphicsQueue.get();
    }
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

std::unique_ptr<RHITimelineSemaphore> VulkanRHIDevice::createTimelineSemaphore(uint64_t initialValue) {
    if (!m_hasTimelineSemaphores) {
        return nullptr;
    }
    return std::make_unique<VulkanRHITimelineSemaphore>(this, initialValue);
}

std::unique_ptr<RHICommandEncoder> VulkanRHIDevice::createCommandEncoder(QueueType queueType) {
    if (queueType == QueueType::Compute && m_hasDedicatedComputeQueue) {
        return std::make_unique<VulkanRHICommandEncoder>(this, *m_computeCommandPool);
    }
    return createCommandEncoder();
}

void VulkanRHIDevice::waitIdle() {
    m_device.waitIdle();
}

void VulkanRHIDevice::logMemoryStats() const {
    if (!m_vmaAllocator) return;

    VmaTotalStatistics stats;
    vmaCalculateStatistics(m_vmaAllocator, &stats);

    auto& total = stats.total;
    std::cout << "[GPU Memory] Allocations: " << total.statistics.allocationCount
              << " | Blocks: " << total.statistics.blockCount
              << " | Allocated: " << (total.statistics.allocationBytes / (1024 * 1024)) << " MB"
              << " | Reserved: " << (total.statistics.blockBytes / (1024 * 1024)) << " MB"
              << std::endl;

    // Check lazily allocated memory usage
    auto memProps = m_physicalDevice.getMemoryProperties();
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if (memProps.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eLazilyAllocated) {
            auto& heapStats = stats.memoryType[i];
            if (heapStats.statistics.allocationCount > 0) {
                std::cout << "[GPU Memory] Lazily allocated: " << heapStats.statistics.allocationCount
                          << " allocs, " << (heapStats.statistics.allocationBytes / 1024) << " KB"
                          << std::endl;
            }
        }
    }

    const auto& features = getCapabilities().getFeatures();
    std::cout << "[GPU Memory] Features: aliasing="
              << (features.memoryAliasing ? "yes" : "no")
              << " lazily_allocated="
              << (features.lazilyAllocatedMemory ? "yes" : "no")
              << " dedicated_compute="
              << (features.dedicatedComputeQueue ? "yes" : "no")
              << " timeline_semaphores="
              << (features.timelineSemaphores ? "yes" : "no")
              << std::endl;
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
