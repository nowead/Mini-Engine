#pragma once

#include <rhi/RHI.hpp>

// WebGPU headers
#ifdef __EMSCRIPTEN__
    #include <webgpu/webgpu.h>
#else
    #include <webgpu/webgpu_cpp.h>
#endif

#include <stdexcept>
#include <iostream>

namespace RHI {
namespace WebGPU {

// =============================================================================
// TextureFormat Conversions
// =============================================================================

WGPUTextureFormat ToWGPUFormat(rhi::TextureFormat format);
rhi::TextureFormat FromWGPUFormat(WGPUTextureFormat format);

// =============================================================================
// BufferUsage Conversions
// =============================================================================

WGPUBufferUsageFlags ToWGPUBufferUsage(rhi::BufferUsage usage);

// =============================================================================
// TextureUsage Conversions
// =============================================================================

WGPUTextureUsageFlags ToWGPUTextureUsage(rhi::TextureUsage usage);

// =============================================================================
// ShaderStage Conversions
// =============================================================================

WGPUShaderStageFlags ToWGPUShaderStage(rhi::ShaderStage stage);

// =============================================================================
// Texture Dimension Conversions
// =============================================================================

WGPUTextureDimension ToWGPUTextureDimension(rhi::TextureDimension dimension);
WGPUTextureViewDimension ToWGPUTextureViewDimension(rhi::TextureViewDimension dimension);

// =============================================================================
// Primitive Topology Conversions
// =============================================================================

WGPUPrimitiveTopology ToWGPUTopology(rhi::PrimitiveTopology topology);

// =============================================================================
// Index Format Conversions
// =============================================================================

WGPUIndexFormat ToWGPUIndexFormat(rhi::IndexFormat format);

// =============================================================================
// Cull Mode Conversions
// =============================================================================

WGPUCullMode ToWGPUCullMode(rhi::CullMode mode);

// =============================================================================
// Front Face Conversions
// =============================================================================

WGPUFrontFace ToWGPUFrontFace(rhi::FrontFace face);

// =============================================================================
// Compare Function Conversions
// =============================================================================

WGPUCompareFunction ToWGPUCompareFunc(rhi::CompareOp op);

// =============================================================================
// Blend Factor Conversions
// =============================================================================

WGPUBlendFactor ToWGPUBlendFactor(rhi::BlendFactor factor);

// =============================================================================
// Blend Operation Conversions
// =============================================================================

WGPUBlendOperation ToWGPUBlendOp(rhi::BlendOp op);

// =============================================================================
// Color Write Mask Conversions
// =============================================================================

WGPUColorWriteMaskFlags ToWGPUColorWriteMask(rhi::ColorWriteMask mask);

// =============================================================================
// Load/Store Operation Conversions
// =============================================================================

WGPULoadOp ToWGPULoadOp(rhi::LoadOp op);
WGPUStoreOp ToWGPUStoreOp(rhi::StoreOp op);

// =============================================================================
// Address Mode Conversions
// =============================================================================

WGPUAddressMode ToWGPUAddressMode(rhi::AddressMode mode);

// =============================================================================
// Filter Mode Conversions
// =============================================================================

WGPUFilterMode ToWGPUFilterMode(rhi::FilterMode mode);
WGPUMipmapFilterMode ToWGPUMipmapFilterMode(rhi::MipmapMode mode);

// =============================================================================
// Vertex Format Conversions
// =============================================================================

WGPUVertexFormat ToWGPUVertexFormat(rhi::TextureFormat format);

} // namespace WebGPU
} // namespace RHI
