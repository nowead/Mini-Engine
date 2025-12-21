#pragma once

/**
 * @file Forward.hpp
 * @brief Forward declarations for all RHI types
 * 
 * Include this header when you only need type names without full definitions.
 * This helps reduce compilation times and circular dependency issues.
 */

namespace rhi {

// Core types (RHITypes.hpp)
enum class BackendType;
enum class Format;
enum class TextureUsage;
enum class BufferUsage;
enum class TextureDimension;
enum class AddressMode;
enum class FilterMode;
enum class CompareFunction;
enum class LoadOp;
enum class StoreOp;
enum class PrimitiveTopology;
enum class CullMode;
enum class FrontFace;
enum class BlendFactor;
enum class BlendOp;
enum class ShaderStage;
enum class IndexFormat;
enum class VertexFormat;

struct Extent3D;
struct Origin3D;
struct Color;
struct TextureSubresourceRange;

// Device (RHIDevice.hpp)
class IRHIDevice;

// Queue (RHIQueue.hpp)
class IRHIQueue;

// Buffer (RHIBuffer.hpp)
class IRHIBuffer;

// Texture (RHITexture.hpp)
class IRHITexture;
class IRHITextureView;

// Sampler (RHISampler.hpp)
class IRHISampler;

// Shader (RHIShader.hpp)
class IRHIShaderModule;

// Bind Group (RHIBindGroup.hpp)
class IRHIBindGroupLayout;
class IRHIBindGroup;

// Pipeline (RHIPipeline.hpp)
class IRHIPipelineLayout;
class IRHIRenderPipeline;
class IRHIComputePipeline;

// Command Buffer (RHICommandBuffer.hpp)
class IRHICommandEncoder;
class IRHIRenderPassEncoder;
class IRHIComputePassEncoder;
class IRHICommandBuffer;

// Render Pass (RHIRenderPass.hpp)
class IRHIRenderPass;

// Swapchain (RHISwapchain.hpp)
class IRHISwapchain;

// Sync (RHISync.hpp)
class IRHISemaphore;
class IRHIFence;

// Capabilities (RHICapabilities.hpp)
class IRHICapabilities;

// Shared pointer aliases
template<typename T>
using RHIPtr = std::shared_ptr<T>;

using DevicePtr = RHIPtr<IRHIDevice>;
using QueuePtr = RHIPtr<IRHIQueue>;
using BufferPtr = RHIPtr<IRHIBuffer>;
using TexturePtr = RHIPtr<IRHITexture>;
using TextureViewPtr = RHIPtr<IRHITextureView>;
using SamplerPtr = RHIPtr<IRHISampler>;
using ShaderModulePtr = RHIPtr<IRHIShaderModule>;
using BindGroupLayoutPtr = RHIPtr<IRHIBindGroupLayout>;
using BindGroupPtr = RHIPtr<IRHIBindGroup>;
using PipelineLayoutPtr = RHIPtr<IRHIPipelineLayout>;
using RenderPipelinePtr = RHIPtr<IRHIRenderPipeline>;
using ComputePipelinePtr = RHIPtr<IRHIComputePipeline>;
using CommandEncoderPtr = RHIPtr<IRHICommandEncoder>;
using CommandBufferPtr = RHIPtr<IRHICommandBuffer>;
using RenderPassPtr = RHIPtr<IRHIRenderPass>;
using SwapchainPtr = RHIPtr<IRHISwapchain>;
using SemaphorePtr = RHIPtr<IRHISemaphore>;
using FencePtr = RHIPtr<IRHIFence>;
using CapabilitiesPtr = RHIPtr<IRHICapabilities>;

} // namespace rhi
