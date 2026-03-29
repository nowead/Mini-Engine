#pragma once

#include <rhi/RHI.hpp>

// WebGPU headers
#ifdef __EMSCRIPTEN__
    #include <webgpu/webgpu.h>
#else
    #include <webgpu/webgpu_cpp.h>
#endif

// =============================================================================
// Emscripten Version Comparison Macros
// =============================================================================
#ifdef __EMSCRIPTEN__
    #define EMSCRIPTEN_VERSION_LESS_THAN(major, minor, tiny) \
        ((__EMSCRIPTEN_major__ < (major)) || \
         ((__EMSCRIPTEN_major__ == (major)) && (__EMSCRIPTEN_minor__ < (minor))) || \
         ((__EMSCRIPTEN_major__ == (major)) && (__EMSCRIPTEN_minor__ == (minor)) && (__EMSCRIPTEN_tiny__ < (tiny))))

    #define EMSCRIPTEN_VERSION_AT_LEAST(major, minor, tiny) \
        (!EMSCRIPTEN_VERSION_LESS_THAN(major, minor, tiny))

    // =========================================================================
    // emdawnwebgpu API Compatibility Layer
    // =========================================================================
    // emdawnwebgpu removes the `Flags` suffix from bitfield typedefs
    typedef WGPUBufferUsage    WGPUBufferUsageFlags;
    typedef WGPUTextureUsage   WGPUTextureUsageFlags;
    typedef WGPUShaderStage    WGPUShaderStageFlags;
    typedef WGPUColorWriteMask WGPUColorWriteMaskFlags;
    typedef WGPUMapMode        WGPUMapModeFlags;

    // emdawnwebgpu renames WGPUBufferMapAsyncStatus → WGPUMapAsyncStatus
    typedef WGPUMapAsyncStatus WGPUBufferMapAsyncStatus;
    #define WGPUBufferMapAsyncStatus_Unknown WGPUMapAsyncStatus_Error
    #define WGPUBufferMapAsyncStatus_Success WGPUMapAsyncStatus_Success

    // emdawnwebgpu renames WGPUShaderModuleWGSLDescriptor → WGPUShaderSourceWGSL
    typedef WGPUShaderSourceWGSL WGPUShaderModuleWGSLDescriptor;
    #define WGPUSType_ShaderModuleWGSLDescriptor WGPUSType_ShaderSourceWGSL

    // emdawnwebgpu removes WGPUErrorType_DeviceLost and WGPUDeviceLostReason_Undefined
    #define WGPUErrorType_DeviceLost       WGPUErrorType_Unknown
    #define WGPUDeviceLostReason_Undefined WGPUDeviceLostReason_Unknown

    // emdawnwebgpu renames WGPUImageCopyBuffer/Texture
    typedef WGPUTexelCopyBufferInfo  WGPUImageCopyBuffer;
    typedef WGPUTexelCopyTextureInfo WGPUImageCopyTexture;

    // emdawnwebgpu removes WGPUQueueWorkDoneStatus_Unknown
    #define WGPUQueueWorkDoneStatus_Unknown WGPUQueueWorkDoneStatus_Error

    // WGPUStringView helper — all label/string fields changed from const char* to WGPUStringView
    #include <cstring>
    inline WGPUStringView wgpuStr(const char* s) {
        return s ? WGPUStringView{s, WGPU_STRLEN} : WGPUStringView{nullptr, 0};
    }
    #define WGPU_LABEL(s) wgpuStr(s)

    // depthWriteEnabled: WGPUBool -> WGPUOptionalBool
    inline WGPUOptionalBool wgpuBoolOpt(bool v) {
        return v ? WGPUOptionalBool_True : WGPUOptionalBool_False;
    }
    #define WGPU_BOOL(v) wgpuBoolOpt(v)

    // WGPUSurfaceDescriptorFromCanvasHTMLSelector renamed in emdawnwebgpu
    typedef WGPUEmscriptenSurfaceSourceCanvasHTMLSelector WGPUSurfaceDescriptorFromCanvasHTMLSelector;
    #define WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector
#else
    // Native build: keep const char* assignment compatible via identity macro
    #define WGPU_LABEL(s) (s)
    #define WGPU_BOOL(v)  (v)
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
