#include <rhi-vulkan/VulkanRHICapabilities.hpp>

namespace RHI {
namespace Vulkan {

VulkanRHICapabilities::VulkanRHICapabilities(const vk::raii::PhysicalDevice& physicalDevice) {
    m_deviceProperties = physicalDevice.getProperties();
    m_deviceFeatures = physicalDevice.getFeatures();

    queryLimits(physicalDevice);
    queryFeatures(physicalDevice);
}

void VulkanRHICapabilities::queryLimits(const vk::raii::PhysicalDevice& physicalDevice) {
    const auto& limits = m_deviceProperties.limits;

    // Texture limits
    m_limits.maxTextureDimension1D = limits.maxImageDimension1D;
    m_limits.maxTextureDimension2D = limits.maxImageDimension2D;
    m_limits.maxTextureDimension3D = limits.maxImageDimension3D;
    m_limits.maxTextureArrayLayers = limits.maxImageArrayLayers;

    // Bind group limits
    m_limits.maxBindGroups = limits.maxBoundDescriptorSets;
    m_limits.maxBindingsPerBindGroup = limits.maxDescriptorSetUniformBuffers;
    m_limits.maxDynamicUniformBuffersPerPipelineLayout = limits.maxDescriptorSetUniformBuffersDynamic;
    m_limits.maxDynamicStorageBuffersPerPipelineLayout = limits.maxDescriptorSetStorageBuffersDynamic;

    // Buffer limits
    m_limits.maxUniformBufferBindingSize = limits.maxUniformBufferRange;
    m_limits.maxStorageBufferBindingSize = limits.maxStorageBufferRange;

    // Vertex input limits
    m_limits.maxVertexBuffers = limits.maxVertexInputBindings;
    m_limits.maxVertexAttributes = limits.maxVertexInputAttributes;
    m_limits.maxVertexBufferArrayStride = limits.maxVertexInputBindingStride;

    // Render target limits
    m_limits.maxColorAttachments = limits.maxColorAttachments;

    // Compute limits
    m_limits.maxComputeWorkgroupSizeX = limits.maxComputeWorkGroupSize[0];
    m_limits.maxComputeWorkgroupSizeY = limits.maxComputeWorkGroupSize[1];
    m_limits.maxComputeWorkgroupSizeZ = limits.maxComputeWorkGroupSize[2];
    m_limits.maxComputeWorkgroupsPerDimension = limits.maxComputeWorkGroupCount[0];
    m_limits.maxComputeInvocationsPerWorkgroup = limits.maxComputeWorkGroupInvocations;

    // Sampler limits
    m_limits.maxSamplerAnisotropy = static_cast<uint32_t>(limits.maxSamplerAnisotropy);

    // Memory alignment limits
    m_limits.minUniformBufferOffsetAlignment = limits.minUniformBufferOffsetAlignment;
    m_limits.minStorageBufferOffsetAlignment = limits.minStorageBufferOffsetAlignment;
}

void VulkanRHICapabilities::queryFeatures(const vk::raii::PhysicalDevice& physicalDevice) {
    // Texture compression
    m_features.textureCompressionBC = m_deviceFeatures.textureCompressionBC;
    m_features.textureCompressionETC2 = m_deviceFeatures.textureCompressionETC2;
    m_features.textureCompressionASTC = m_deviceFeatures.textureCompressionASTC_LDR;

    // Draw features
    m_features.multiDrawIndirect = m_deviceFeatures.multiDrawIndirect;
    m_features.indirectFirstInstance = m_deviceFeatures.drawIndirectFirstInstance;

    // Query features
    m_features.timestampQuery = true; // Vulkan always supports timestamps
    m_features.occlusionQuery = m_deviceFeatures.occlusionQueryPrecise;
    m_features.pipelineStatisticsQuery = m_deviceFeatures.pipelineStatisticsQuery;

    // Shader features
    m_features.geometryShader = m_deviceFeatures.geometryShader;
    m_features.tessellationShader = m_deviceFeatures.tessellationShader;
    m_features.computeShader = true; // Vulkan 1.0 core feature

    // Query Vulkan 1.2 features using RAII API
    vk::PhysicalDeviceVulkan12Features features12{};
    vk::PhysicalDeviceFeatures2 features2{};
    features2.pNext = &features12;
    features2 = physicalDevice.getFeatures2();

    m_features.shaderFloat16 = features12.shaderFloat16;

    // Ray tracing not yet supported in this implementation
    m_features.rayTracing = false;
    m_features.rayTracingPipeline = false;
    m_features.rayQuery = false;

    // Mesh shading not yet supported
    m_features.meshShader = false;
    m_features.taskShader = false;

    // Other features
    m_features.dualSourceBlend = m_deviceFeatures.dualSrcBlend;
    m_features.logicOp = m_deviceFeatures.logicOp;
    m_features.sampleRateShading = m_deviceFeatures.sampleRateShading;
    m_features.wideLines = m_deviceFeatures.wideLines;
    m_features.largePoints = m_deviceFeatures.largePoints;
}

bool VulkanRHICapabilities::isFormatSupported(TextureFormat format,
                                               TextureUsage usage) const {
    // TODO: Implement proper format support checking
    // For now, assume common formats are supported
    return true;
}

bool VulkanRHICapabilities::isSampleCountSupported(TextureFormat format,
                                                    uint32_t sampleCount) const {
    // TODO: Implement proper sample count checking
    // For now, assume common sample counts (1, 2, 4, 8) are supported
    return sampleCount <= 8 && (sampleCount & (sampleCount - 1)) == 0;
}

} // namespace Vulkan
} // namespace RHI
