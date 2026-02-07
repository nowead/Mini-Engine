#pragma once

#include "RHITypes.hpp"
#include <cstdint>

namespace rhi {

/**
 * @brief Hardware and API limits
 */
struct RHILimits {
    // Texture limits
    uint32_t maxTextureDimension1D = 8192;
    uint32_t maxTextureDimension2D = 8192;
    uint32_t maxTextureDimension3D = 2048;
    uint32_t maxTextureArrayLayers = 256;

    // Bind group limits
    uint32_t maxBindGroups = 4;
    uint32_t maxBindingsPerBindGroup = 1000;
    uint32_t maxDynamicUniformBuffersPerPipelineLayout = 8;
    uint32_t maxDynamicStorageBuffersPerPipelineLayout = 4;

    // Buffer limits
    uint32_t maxUniformBufferBindingSize = 65536;        // 64 KB
    uint32_t maxStorageBufferBindingSize = 134217728;    // 128 MB

    // Vertex input limits
    uint32_t maxVertexBuffers = 8;
    uint32_t maxVertexAttributes = 16;
    uint32_t maxVertexBufferArrayStride = 2048;

    // Render target limits
    uint32_t maxColorAttachments = 8;

    // Compute limits
    uint32_t maxComputeWorkgroupSizeX = 256;
    uint32_t maxComputeWorkgroupSizeY = 256;
    uint32_t maxComputeWorkgroupSizeZ = 64;
    uint32_t maxComputeWorkgroupsPerDimension = 65535;
    uint32_t maxComputeInvocationsPerWorkgroup = 256;

    // Sampler limits
    uint32_t maxSamplerAnisotropy = 16;

    // Memory limits
    uint64_t minUniformBufferOffsetAlignment = 256;
    uint64_t minStorageBufferOffsetAlignment = 256;
};

/**
 * @brief Optional features supported by the backend
 */
struct RHIFeatures {
    // Depth/stencil features
    bool depthClipControl = false;
    bool depth32FloatStencil8 = false;
    bool depth24UnormStencil8 = false;

    // Query features
    bool timestampQuery = false;
    bool pipelineStatisticsQuery = false;
    bool occlusionQuery = false;

    // Texture compression features
    bool textureCompressionBC = false;      // BC1-BC7 (Desktop)
    bool textureCompressionETC2 = false;    // ETC2/EAC (Mobile)
    bool textureCompressionASTC = false;    // ASTC (Mobile)

    // Draw features
    bool indirectFirstInstance = false;
    bool multiDrawIndirect = false;
    bool drawIndirectCount = false;

    // Shader features
    bool shaderFloat16 = false;
    bool shaderInt16 = false;
    bool shaderInt64 = false;

    // Advanced features
    bool geometryShader = false;
    bool tessellationShader = false;
    bool computeShader = true;  // Usually supported

    // Ray tracing (optional, high-end GPUs only)
    bool rayTracing = false;
    bool rayTracingPipeline = false;
    bool rayQuery = false;

    // Mesh shading (optional, modern GPUs)
    bool meshShader = false;
    bool taskShader = false;

    // Variable rate shading
    bool variableRateShading = false;

    // Sampler features
    bool samplerAnisotropy = true;
    bool samplerMirrorClampToEdge = false;

    // Blend features
    bool dualSourceBlend = false;
    bool logicOp = false;

    // Multisampling features
    bool sampleRateShading = false;

    // Fill mode features
    bool fillModeNonSolid = true;  // Wireframe mode

    // Wide lines
    bool wideLines = false;

    // Large points
    bool largePoints = false;

    // Phase 3.1: Memory aliasing
    bool memoryAliasing = false;            // VMA aliasing support
    bool lazilyAllocatedMemory = false;     // Lazily allocated memory for transient attachments

    // Phase 3.2: Async compute
    bool dedicatedComputeQueue = false;     // Separate compute queue available
    bool timelineSemaphores = false;        // Timeline semaphore support
};

/**
 * @brief RHI capabilities interface for querying hardware and API features
 */
class RHICapabilities {
public:
    virtual ~RHICapabilities() = default;

    /**
     * @brief Get hardware and API limits
     * @return Reference to limits structure
     */
    virtual const RHILimits& getLimits() const = 0;

    /**
     * @brief Get optional features
     * @return Reference to features structure
     */
    virtual const RHIFeatures& getFeatures() const = 0;

    /**
     * @brief Check if a texture format is supported for specific usage
     * @param format Texture format to check
     * @param usage Intended usage flags
     * @return true if format is supported for the usage, false otherwise
     */
    virtual bool isFormatSupported(TextureFormat format, TextureUsage usage) const = 0;

    /**
     * @brief Check if a sample count is supported for a texture format
     * @param format Texture format
     * @param sampleCount Sample count (1, 2, 4, 8, 16, 32, 64)
     * @return true if sample count is supported, false otherwise
     */
    virtual bool isSampleCountSupported(TextureFormat format, uint32_t sampleCount) const = 0;
};

} // namespace rhi
