#include <rhi/webgpu/WebGPURHICapabilities.hpp>
#include <rhi/webgpu/WebGPURHIDevice.hpp>

namespace RHI {
namespace WebGPU {

WebGPURHICapabilities::WebGPURHICapabilities(WebGPURHIDevice* device)
    : m_device(device)
{
    queryLimits(device->getWGPUDevice());
    queryFeatures(device->getWGPUDevice());
}

WebGPURHICapabilities::~WebGPURHICapabilities() {
}

void WebGPURHICapabilities::queryLimits(WGPUDevice device) {
#ifdef __EMSCRIPTEN__
    // Emscripten: Use WebGPU guaranteed minimum limits
    // wgpuDeviceGetLimits doesn't work properly in Emscripten
    m_limits.maxTextureDimension1D = 8192;
    m_limits.maxTextureDimension2D = 8192;
    m_limits.maxTextureDimension3D = 2048;
    m_limits.maxTextureArrayLayers = 256;

    m_limits.maxBindGroups = 4;
    m_limits.maxBindingsPerBindGroup = 640;
    m_limits.maxDynamicUniformBuffersPerPipelineLayout = 8;
    m_limits.maxDynamicStorageBuffersPerPipelineLayout = 4;

    m_limits.maxUniformBufferBindingSize = 65536;
    m_limits.maxStorageBufferBindingSize = 134217728;

    m_limits.maxVertexBuffers = 8;
    m_limits.maxVertexAttributes = 16;
    m_limits.maxVertexBufferArrayStride = 2048;

    m_limits.maxColorAttachments = 8;

    m_limits.maxComputeWorkgroupSizeX = 256;
    m_limits.maxComputeWorkgroupSizeY = 256;
    m_limits.maxComputeWorkgroupSizeZ = 64;
    m_limits.maxComputeWorkgroupsPerDimension = 65535;
    m_limits.maxComputeInvocationsPerWorkgroup = 256;

    m_limits.maxSamplerAnisotropy = 16;

    m_limits.minUniformBufferOffsetAlignment = 256;
    m_limits.minStorageBufferOffsetAlignment = 256;
#else
    // Native: Query actual device limits
    WGPUSupportedLimits supportedLimits{};
    wgpuDeviceGetLimits(device, &supportedLimits);

    const WGPULimits& limits = supportedLimits.limits;

    m_limits.maxTextureDimension1D = limits.maxTextureDimension1D;
    m_limits.maxTextureDimension2D = limits.maxTextureDimension2D;
    m_limits.maxTextureDimension3D = limits.maxTextureDimension3D;
    m_limits.maxTextureArrayLayers = limits.maxTextureArrayLayers;

    m_limits.maxBindGroups = limits.maxBindGroups;
    m_limits.maxBindingsPerBindGroup = limits.maxBindingsPerBindGroup;
    m_limits.maxDynamicUniformBuffersPerPipelineLayout = limits.maxDynamicUniformBuffersPerPipelineLayout;
    m_limits.maxDynamicStorageBuffersPerPipelineLayout = limits.maxDynamicStorageBuffersPerPipelineLayout;

    m_limits.maxUniformBufferBindingSize = limits.maxUniformBufferBindingSize;
    m_limits.maxStorageBufferBindingSize = limits.maxStorageBufferBindingSize;

    m_limits.maxVertexBuffers = limits.maxVertexBuffers;
    m_limits.maxVertexAttributes = limits.maxVertexAttributes;
    m_limits.maxVertexBufferArrayStride = limits.maxVertexBufferArrayStride;

    m_limits.maxColorAttachments = limits.maxColorAttachments;

    m_limits.maxComputeWorkgroupSizeX = limits.maxComputeWorkgroupSizeX;
    m_limits.maxComputeWorkgroupSizeY = limits.maxComputeWorkgroupSizeY;
    m_limits.maxComputeWorkgroupSizeZ = limits.maxComputeWorkgroupSizeZ;
    m_limits.maxComputeWorkgroupsPerDimension = limits.maxComputeWorkgroupsPerDimension;
    m_limits.maxComputeInvocationsPerWorkgroup = limits.maxComputeInvocationsPerWorkgroup;

    m_limits.maxSamplerAnisotropy = 16;

    m_limits.minUniformBufferOffsetAlignment = limits.minUniformBufferOffsetAlignment;
    m_limits.minStorageBufferOffsetAlignment = limits.minStorageBufferOffsetAlignment;
#endif
}

void WebGPURHICapabilities::queryFeatures(WGPUDevice device) {
    // WebGPU has a more limited feature set compared to Vulkan
    // Most features are either always available or not available

    // Depth/stencil features
    m_features.depthClipControl = false; // Not in WebGPU core
    m_features.depth32FloatStencil8 = true; // WebGPU supports this
    m_features.depth24UnormStencil8 = true;

    // Query features
    m_features.timestampQuery = false; // Requires extension
    m_features.pipelineStatisticsQuery = false;
    m_features.occlusionQuery = false;

    // Texture compression
    m_features.textureCompressionBC = false; // Desktop GPUs might support via extensions
    m_features.textureCompressionETC2 = false;
    m_features.textureCompressionASTC = false;

    // Draw features
    m_features.indirectFirstInstance = true;
    m_features.multiDrawIndirect = false; // Not in WebGPU core
    m_features.drawIndirectCount = false;

    // Shader features
    m_features.shaderFloat16 = false; // Requires extension
    m_features.shaderInt16 = false;
    m_features.shaderInt64 = false;

    // Advanced features
    m_features.geometryShader = false; // WebGPU doesn't support geometry shaders
    m_features.tessellationShader = false; // WebGPU doesn't support tessellation
    m_features.computeShader = true; // Always supported in WebGPU

    // Ray tracing
    m_features.rayTracing = false; // Not in WebGPU
    m_features.rayTracingPipeline = false;
    m_features.rayQuery = false;

    // Mesh shading
    m_features.meshShader = false; // Not in WebGPU core
    m_features.taskShader = false;

    // Variable rate shading
    m_features.variableRateShading = false;

    // Sampler features
    m_features.samplerAnisotropy = true; // Always supported
    m_features.samplerMirrorClampToEdge = false;

    // Blend features
    m_features.dualSourceBlend = false;
    m_features.logicOp = false;

    // Multisampling
    m_features.sampleRateShading = false;

    // Fill mode
    m_features.fillModeNonSolid = true; // Wireframe supported

    // Lines and points
    m_features.wideLines = false;
    m_features.largePoints = false;
}

bool WebGPURHICapabilities::isFormatSupported(TextureFormat format, TextureUsage usage) const {
    // WebGPU has good format support - most common formats are supported
    // For simplicity, we'll return true for most formats
    // In a real implementation, you would check format capabilities via wgpuAdapterGetFormatCapabilities

    switch (format) {
        // Common color formats - always supported
        case TextureFormat::RGBA8Unorm:
        case TextureFormat::RGBA8UnormSrgb:
        case TextureFormat::BGRA8Unorm:
        case TextureFormat::BGRA8UnormSrgb:
        case TextureFormat::RGBA16Float:
        case TextureFormat::RGBA32Float:
        case TextureFormat::R8Unorm:
        case TextureFormat::RG8Unorm:
        case TextureFormat::R16Float:
        case TextureFormat::RG16Float:
        case TextureFormat::R32Float:
        case TextureFormat::RG32Float:
            return true;

        // Depth/stencil formats
        case TextureFormat::Depth16Unorm:
        case TextureFormat::Depth24Plus:
        case TextureFormat::Depth32Float:
        case TextureFormat::Depth24PlusStencil8:
            return hasFlag(usage, TextureUsage::RenderTarget);

        // Integer formats
        case TextureFormat::R32Uint:
        case TextureFormat::R32Sint:
        case TextureFormat::RG32Uint:
        case TextureFormat::RG32Sint:
        case TextureFormat::RGBA32Uint:
        case TextureFormat::RGBA32Sint:
            return true;

        default:
            return false; // Unsupported or compressed formats
    }
}

bool WebGPURHICapabilities::isSampleCountSupported(TextureFormat format, uint32_t sampleCount) const {
    // WebGPU typically supports 1 and 4 samples for most formats
    if (sampleCount == 1) {
        return true; // Single sample always supported
    }

    if (sampleCount == 4) {
        // 4x MSAA supported for most color and depth formats
        switch (format) {
            case TextureFormat::RGBA8Unorm:
            case TextureFormat::RGBA8UnormSrgb:
            case TextureFormat::BGRA8Unorm:
            case TextureFormat::BGRA8UnormSrgb:
            case TextureFormat::RGBA16Float:
            case TextureFormat::Depth24Plus:
            case TextureFormat::Depth32Float:
            case TextureFormat::Depth24PlusStencil8:
                return true;
            default:
                return false;
        }
    }

    // Other sample counts not typically supported
    return false;
}

} // namespace WebGPU
} // namespace RHI
