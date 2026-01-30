#include "rhi/webgpu/WebGPUCommon.hpp"

namespace RHI {
namespace WebGPU {

// =============================================================================
// TextureFormat Conversions
// =============================================================================

WGPUTextureFormat ToWGPUFormat(rhi::TextureFormat format) {
    switch (format) {
        // 8-bit formats
        case rhi::TextureFormat::R8Unorm:           return WGPUTextureFormat_R8Unorm;
        case rhi::TextureFormat::R8Snorm:           return WGPUTextureFormat_R8Snorm;
        case rhi::TextureFormat::R8Uint:            return WGPUTextureFormat_R8Uint;
        case rhi::TextureFormat::R8Sint:            return WGPUTextureFormat_R8Sint;

        // 16-bit formats
        case rhi::TextureFormat::R16Uint:           return WGPUTextureFormat_R16Uint;
        case rhi::TextureFormat::R16Sint:           return WGPUTextureFormat_R16Sint;
        case rhi::TextureFormat::R16Float:          return WGPUTextureFormat_R16Float;
        case rhi::TextureFormat::RG8Unorm:          return WGPUTextureFormat_RG8Unorm;
        case rhi::TextureFormat::RG8Snorm:          return WGPUTextureFormat_RG8Snorm;
        case rhi::TextureFormat::RG8Uint:           return WGPUTextureFormat_RG8Uint;
        case rhi::TextureFormat::RG8Sint:           return WGPUTextureFormat_RG8Sint;

        // 32-bit formats
        case rhi::TextureFormat::R32Uint:           return WGPUTextureFormat_R32Uint;
        case rhi::TextureFormat::R32Sint:           return WGPUTextureFormat_R32Sint;
        case rhi::TextureFormat::R32Float:          return WGPUTextureFormat_R32Float;
        case rhi::TextureFormat::RG16Uint:          return WGPUTextureFormat_RG16Uint;
        case rhi::TextureFormat::RG16Sint:          return WGPUTextureFormat_RG16Sint;
        case rhi::TextureFormat::RG16Float:         return WGPUTextureFormat_RG16Float;
        case rhi::TextureFormat::RGBA8Unorm:        return WGPUTextureFormat_RGBA8Unorm;
        case rhi::TextureFormat::RGBA8UnormSrgb:    return WGPUTextureFormat_RGBA8UnormSrgb;
        case rhi::TextureFormat::RGBA8Snorm:        return WGPUTextureFormat_RGBA8Snorm;
        case rhi::TextureFormat::RGBA8Uint:         return WGPUTextureFormat_RGBA8Uint;
        case rhi::TextureFormat::RGBA8Sint:         return WGPUTextureFormat_RGBA8Sint;
        case rhi::TextureFormat::BGRA8Unorm:        return WGPUTextureFormat_BGRA8Unorm;
        case rhi::TextureFormat::BGRA8UnormSrgb:    return WGPUTextureFormat_BGRA8UnormSrgb;

        // 64-bit formats
        case rhi::TextureFormat::RG32Uint:          return WGPUTextureFormat_RG32Uint;
        case rhi::TextureFormat::RG32Sint:          return WGPUTextureFormat_RG32Sint;
        case rhi::TextureFormat::RG32Float:         return WGPUTextureFormat_RG32Float;
        case rhi::TextureFormat::RGBA16Uint:        return WGPUTextureFormat_RGBA16Uint;
        case rhi::TextureFormat::RGBA16Sint:        return WGPUTextureFormat_RGBA16Sint;
        case rhi::TextureFormat::RGBA16Float:       return WGPUTextureFormat_RGBA16Float;

        // 128-bit formats
        case rhi::TextureFormat::RGBA32Uint:        return WGPUTextureFormat_RGBA32Uint;
        case rhi::TextureFormat::RGBA32Sint:        return WGPUTextureFormat_RGBA32Sint;
        case rhi::TextureFormat::RGBA32Float:       return WGPUTextureFormat_RGBA32Float;

        // Depth/Stencil formats
        case rhi::TextureFormat::Depth32Float:      return WGPUTextureFormat_Depth32Float;
        case rhi::TextureFormat::Depth24Plus:       return WGPUTextureFormat_Depth24Plus;
        case rhi::TextureFormat::Depth24PlusStencil8: return WGPUTextureFormat_Depth24PlusStencil8;

        // Unsupported formats (fallback)
        case rhi::TextureFormat::Depth16Unorm:
            std::cerr << "[WebGPU] Warning: Depth16Unorm not supported, using Depth24Plus fallback\n";
            return WGPUTextureFormat_Depth24Plus;

        case rhi::TextureFormat::RGB32Uint:
        case rhi::TextureFormat::RGB32Sint:
        case rhi::TextureFormat::RGB32Float:
            throw std::runtime_error("RGB32 formats not supported in WebGPU (use RGBA32 instead)");

        case rhi::TextureFormat::Undefined:
        default:
            throw std::runtime_error("Unsupported or undefined texture format for WebGPU");
    }
}

rhi::TextureFormat FromWGPUFormat(WGPUTextureFormat format) {
    switch (format) {
        case WGPUTextureFormat_R8Unorm:             return rhi::TextureFormat::R8Unorm;
        case WGPUTextureFormat_R8Snorm:             return rhi::TextureFormat::R8Snorm;
        case WGPUTextureFormat_R8Uint:              return rhi::TextureFormat::R8Uint;
        case WGPUTextureFormat_R8Sint:              return rhi::TextureFormat::R8Sint;
        case WGPUTextureFormat_RGBA8Unorm:          return rhi::TextureFormat::RGBA8Unorm;
        case WGPUTextureFormat_RGBA8UnormSrgb:      return rhi::TextureFormat::RGBA8UnormSrgb;
        case WGPUTextureFormat_BGRA8Unorm:          return rhi::TextureFormat::BGRA8Unorm;
        case WGPUTextureFormat_BGRA8UnormSrgb:      return rhi::TextureFormat::BGRA8UnormSrgb;
        case WGPUTextureFormat_Depth32Float:        return rhi::TextureFormat::Depth32Float;
        case WGPUTextureFormat_Depth24Plus:         return rhi::TextureFormat::Depth24Plus;
        case WGPUTextureFormat_Depth24PlusStencil8: return rhi::TextureFormat::Depth24PlusStencil8;
        default:                                     return rhi::TextureFormat::Undefined;
    }
}

// =============================================================================
// BufferUsage Conversions
// =============================================================================

WGPUBufferUsageFlags ToWGPUBufferUsage(rhi::BufferUsage usage) {
    WGPUBufferUsageFlags flags = WGPUBufferUsage_None;

    bool hasMapWrite = rhi::hasFlag(usage, rhi::BufferUsage::MapWrite);
    bool hasOtherUsage = rhi::hasFlag(usage, rhi::BufferUsage::Uniform) ||
                         rhi::hasFlag(usage, rhi::BufferUsage::Vertex) ||
                         rhi::hasFlag(usage, rhi::BufferUsage::Index) ||
                         rhi::hasFlag(usage, rhi::BufferUsage::Storage) ||
                         rhi::hasFlag(usage, rhi::BufferUsage::Indirect);

    // WebGPU constraint: MapWrite can only be combined with CopySrc
    // If MapWrite is combined with other usages (Uniform, Vertex, etc.),
    // we replace MapWrite with CopyDst (for queue.writeBuffer support)
    bool useMapWrite = hasMapWrite && !hasOtherUsage;

    if (rhi::hasFlag(usage, rhi::BufferUsage::Vertex))
        flags |= WGPUBufferUsage_Vertex;
    if (rhi::hasFlag(usage, rhi::BufferUsage::Index))
        flags |= WGPUBufferUsage_Index;
    if (rhi::hasFlag(usage, rhi::BufferUsage::Uniform))
        flags |= WGPUBufferUsage_Uniform;
    if (rhi::hasFlag(usage, rhi::BufferUsage::Storage))
        flags |= WGPUBufferUsage_Storage;
    if (rhi::hasFlag(usage, rhi::BufferUsage::CopySrc))
        flags |= WGPUBufferUsage_CopySrc;
    if (rhi::hasFlag(usage, rhi::BufferUsage::CopyDst))
        flags |= WGPUBufferUsage_CopyDst;
    if (rhi::hasFlag(usage, rhi::BufferUsage::Indirect))
        flags |= WGPUBufferUsage_Indirect;
    if (rhi::hasFlag(usage, rhi::BufferUsage::MapRead))
        flags |= WGPUBufferUsage_MapRead;

    if (useMapWrite) {
        flags |= WGPUBufferUsage_MapWrite;
    } else if (hasMapWrite) {
        // MapWrite was requested but can't be used with other usages
        // Add CopyDst to allow queue.writeBuffer instead
        flags |= WGPUBufferUsage_CopyDst;
    }

    return flags;
}

// =============================================================================
// TextureUsage Conversions
// =============================================================================

WGPUTextureUsageFlags ToWGPUTextureUsage(rhi::TextureUsage usage) {
    WGPUTextureUsageFlags flags = WGPUTextureUsage_None;

    if (rhi::hasFlag(usage, rhi::TextureUsage::Sampled))
        flags |= WGPUTextureUsage_TextureBinding;
    if (rhi::hasFlag(usage, rhi::TextureUsage::Storage))
        flags |= WGPUTextureUsage_StorageBinding;
    if (rhi::hasFlag(usage, rhi::TextureUsage::RenderTarget))
        flags |= WGPUTextureUsage_RenderAttachment;
    if (rhi::hasFlag(usage, rhi::TextureUsage::DepthStencil))
        flags |= WGPUTextureUsage_RenderAttachment;
    if (rhi::hasFlag(usage, rhi::TextureUsage::CopySrc))
        flags |= WGPUTextureUsage_CopySrc;
    if (rhi::hasFlag(usage, rhi::TextureUsage::CopyDst))
        flags |= WGPUTextureUsage_CopyDst;

    return flags;
}

// =============================================================================
// ShaderStage Conversions
// =============================================================================

WGPUShaderStageFlags ToWGPUShaderStage(rhi::ShaderStage stage) {
    WGPUShaderStageFlags flags = WGPUShaderStage_None;

    if (rhi::hasFlag(stage, rhi::ShaderStage::Vertex))
        flags |= WGPUShaderStage_Vertex;
    if (rhi::hasFlag(stage, rhi::ShaderStage::Fragment))
        flags |= WGPUShaderStage_Fragment;
    if (rhi::hasFlag(stage, rhi::ShaderStage::Compute))
        flags |= WGPUShaderStage_Compute;

    return flags;
}

// =============================================================================
// Texture Dimension Conversions
// =============================================================================

WGPUTextureDimension ToWGPUTextureDimension(rhi::TextureDimension dimension) {
    switch (dimension) {
        case rhi::TextureDimension::Texture1D: return WGPUTextureDimension_1D;
        case rhi::TextureDimension::Texture2D: return WGPUTextureDimension_2D;
        case rhi::TextureDimension::Texture3D: return WGPUTextureDimension_3D;
        default: throw std::runtime_error("Invalid texture dimension");
    }
}

WGPUTextureViewDimension ToWGPUTextureViewDimension(rhi::TextureViewDimension dimension) {
    switch (dimension) {
        case rhi::TextureViewDimension::View1D:         return WGPUTextureViewDimension_1D;
        case rhi::TextureViewDimension::View2D:         return WGPUTextureViewDimension_2D;
        case rhi::TextureViewDimension::View2DArray:    return WGPUTextureViewDimension_2DArray;
        case rhi::TextureViewDimension::ViewCube:       return WGPUTextureViewDimension_Cube;
        case rhi::TextureViewDimension::ViewCubeArray:  return WGPUTextureViewDimension_CubeArray;
        case rhi::TextureViewDimension::View3D:         return WGPUTextureViewDimension_3D;
        default: throw std::runtime_error("Invalid texture view dimension");
    }
}

// =============================================================================
// Primitive Topology Conversions
// =============================================================================

WGPUPrimitiveTopology ToWGPUTopology(rhi::PrimitiveTopology topology) {
    switch (topology) {
        case rhi::PrimitiveTopology::PointList:     return WGPUPrimitiveTopology_PointList;
        case rhi::PrimitiveTopology::LineList:      return WGPUPrimitiveTopology_LineList;
        case rhi::PrimitiveTopology::LineStrip:     return WGPUPrimitiveTopology_LineStrip;
        case rhi::PrimitiveTopology::TriangleList:  return WGPUPrimitiveTopology_TriangleList;
        case rhi::PrimitiveTopology::TriangleStrip: return WGPUPrimitiveTopology_TriangleStrip;
        default: throw std::runtime_error("Invalid primitive topology");
    }
}

// =============================================================================
// Index Format Conversions
// =============================================================================

WGPUIndexFormat ToWGPUIndexFormat(rhi::IndexFormat format) {
    switch (format) {
        case rhi::IndexFormat::Uint16: return WGPUIndexFormat_Uint16;
        case rhi::IndexFormat::Uint32: return WGPUIndexFormat_Uint32;
        default: throw std::runtime_error("Invalid index format");
    }
}

// =============================================================================
// Cull Mode Conversions
// =============================================================================

WGPUCullMode ToWGPUCullMode(rhi::CullMode mode) {
    switch (mode) {
        case rhi::CullMode::None:  return WGPUCullMode_None;
        case rhi::CullMode::Front: return WGPUCullMode_Front;
        case rhi::CullMode::Back:  return WGPUCullMode_Back;
        default: throw std::runtime_error("Invalid cull mode");
    }
}

// =============================================================================
// Front Face Conversions
// =============================================================================

WGPUFrontFace ToWGPUFrontFace(rhi::FrontFace face) {
    switch (face) {
        case rhi::FrontFace::CounterClockwise: return WGPUFrontFace_CCW;
        case rhi::FrontFace::Clockwise:        return WGPUFrontFace_CW;
        default: throw std::runtime_error("Invalid front face");
    }
}

// =============================================================================
// Compare Function Conversions
// =============================================================================

WGPUCompareFunction ToWGPUCompareFunc(rhi::CompareOp op) {
    switch (op) {
        case rhi::CompareOp::Never:          return WGPUCompareFunction_Never;
        case rhi::CompareOp::Less:           return WGPUCompareFunction_Less;
        case rhi::CompareOp::Equal:          return WGPUCompareFunction_Equal;
        case rhi::CompareOp::LessOrEqual:    return WGPUCompareFunction_LessEqual;
        case rhi::CompareOp::Greater:        return WGPUCompareFunction_Greater;
        case rhi::CompareOp::NotEqual:       return WGPUCompareFunction_NotEqual;
        case rhi::CompareOp::GreaterOrEqual: return WGPUCompareFunction_GreaterEqual;
        case rhi::CompareOp::Always:         return WGPUCompareFunction_Always;
        default: throw std::runtime_error("Invalid compare operation");
    }
}

// =============================================================================
// Blend Factor Conversions
// =============================================================================

WGPUBlendFactor ToWGPUBlendFactor(rhi::BlendFactor factor) {
    switch (factor) {
        case rhi::BlendFactor::Zero:                  return WGPUBlendFactor_Zero;
        case rhi::BlendFactor::One:                   return WGPUBlendFactor_One;
        case rhi::BlendFactor::SrcColor:              return WGPUBlendFactor_Src;
        case rhi::BlendFactor::OneMinusSrcColor:      return WGPUBlendFactor_OneMinusSrc;
        case rhi::BlendFactor::DstColor:              return WGPUBlendFactor_Dst;
        case rhi::BlendFactor::OneMinusDstColor:      return WGPUBlendFactor_OneMinusDst;
        case rhi::BlendFactor::SrcAlpha:              return WGPUBlendFactor_SrcAlpha;
        case rhi::BlendFactor::OneMinusSrcAlpha:      return WGPUBlendFactor_OneMinusSrcAlpha;
        case rhi::BlendFactor::DstAlpha:              return WGPUBlendFactor_DstAlpha;
        case rhi::BlendFactor::OneMinusDstAlpha:      return WGPUBlendFactor_OneMinusDstAlpha;
        case rhi::BlendFactor::ConstantColor:         return WGPUBlendFactor_Constant;
        case rhi::BlendFactor::OneMinusConstantColor: return WGPUBlendFactor_OneMinusConstant;
        // Note: WebGPU doesn't have separate ConstantAlpha
        case rhi::BlendFactor::ConstantAlpha:         return WGPUBlendFactor_Constant;
        case rhi::BlendFactor::OneMinusConstantAlpha: return WGPUBlendFactor_OneMinusConstant;
        default: throw std::runtime_error("Invalid blend factor");
    }
}

// =============================================================================
// Blend Operation Conversions
// =============================================================================

WGPUBlendOperation ToWGPUBlendOp(rhi::BlendOp op) {
    switch (op) {
        case rhi::BlendOp::Add:             return WGPUBlendOperation_Add;
        case rhi::BlendOp::Subtract:        return WGPUBlendOperation_Subtract;
        case rhi::BlendOp::ReverseSubtract: return WGPUBlendOperation_ReverseSubtract;
        case rhi::BlendOp::Min:             return WGPUBlendOperation_Min;
        case rhi::BlendOp::Max:             return WGPUBlendOperation_Max;
        default: throw std::runtime_error("Invalid blend operation");
    }
}

// =============================================================================
// Color Write Mask Conversions
// =============================================================================

WGPUColorWriteMaskFlags ToWGPUColorWriteMask(rhi::ColorWriteMask mask) {
    WGPUColorWriteMaskFlags flags = WGPUColorWriteMask_None;

    auto maskValue = static_cast<uint32_t>(mask);
    auto redValue = static_cast<uint32_t>(rhi::ColorWriteMask::Red);
    auto greenValue = static_cast<uint32_t>(rhi::ColorWriteMask::Green);
    auto blueValue = static_cast<uint32_t>(rhi::ColorWriteMask::Blue);
    auto alphaValue = static_cast<uint32_t>(rhi::ColorWriteMask::Alpha);

    if ((maskValue & redValue) != 0)   flags |= WGPUColorWriteMask_Red;
    if ((maskValue & greenValue) != 0) flags |= WGPUColorWriteMask_Green;
    if ((maskValue & blueValue) != 0)  flags |= WGPUColorWriteMask_Blue;
    if ((maskValue & alphaValue) != 0) flags |= WGPUColorWriteMask_Alpha;

    return flags;
}

// =============================================================================
// Load/Store Operation Conversions
// =============================================================================

WGPULoadOp ToWGPULoadOp(rhi::LoadOp op) {
    switch (op) {
        case rhi::LoadOp::Load:     return WGPULoadOp_Load;
        case rhi::LoadOp::Clear:    return WGPULoadOp_Clear;
        case rhi::LoadOp::DontCare: return WGPULoadOp_Clear;  // WebGPU doesn't have DontCare
        default: throw std::runtime_error("Invalid load operation");
    }
}

WGPUStoreOp ToWGPUStoreOp(rhi::StoreOp op) {
    switch (op) {
        case rhi::StoreOp::Store:    return WGPUStoreOp_Store;
        case rhi::StoreOp::DontCare: return WGPUStoreOp_Discard;
        default: throw std::runtime_error("Invalid store operation");
    }
}

// =============================================================================
// Address Mode Conversions
// =============================================================================

WGPUAddressMode ToWGPUAddressMode(rhi::AddressMode mode) {
    switch (mode) {
        case rhi::AddressMode::Repeat:       return WGPUAddressMode_Repeat;
        case rhi::AddressMode::MirrorRepeat: return WGPUAddressMode_MirrorRepeat;
        case rhi::AddressMode::ClampToEdge:  return WGPUAddressMode_ClampToEdge;
        // WebGPU doesn't have ClampToBorder, use ClampToEdge
        case rhi::AddressMode::ClampToBorder: return WGPUAddressMode_ClampToEdge;
        default: throw std::runtime_error("Invalid address mode");
    }
}

// =============================================================================
// Filter Mode Conversions
// =============================================================================

WGPUFilterMode ToWGPUFilterMode(rhi::FilterMode mode) {
    switch (mode) {
        case rhi::FilterMode::Nearest: return WGPUFilterMode_Nearest;
        case rhi::FilterMode::Linear:  return WGPUFilterMode_Linear;
        default: throw std::runtime_error("Invalid filter mode");
    }
}

WGPUMipmapFilterMode ToWGPUMipmapFilterMode(rhi::MipmapMode mode) {
    switch (mode) {
        case rhi::MipmapMode::Nearest: return WGPUMipmapFilterMode_Nearest;
        case rhi::MipmapMode::Linear:  return WGPUMipmapFilterMode_Linear;
        default: throw std::runtime_error("Invalid mipmap filter mode");
    }
}

// =============================================================================
// Vertex Format Conversions
// =============================================================================

WGPUVertexFormat ToWGPUVertexFormat(rhi::TextureFormat format) {
    switch (format) {
        case rhi::TextureFormat::R8Unorm:    return WGPUVertexFormat_Unorm8x2; // Closest match
        case rhi::TextureFormat::RG8Unorm:   return WGPUVertexFormat_Unorm8x2;
        case rhi::TextureFormat::RGBA8Unorm: return WGPUVertexFormat_Unorm8x4;
        case rhi::TextureFormat::R16Float:   return WGPUVertexFormat_Float16x2; // Closest match
        case rhi::TextureFormat::RG16Float:  return WGPUVertexFormat_Float16x2;
        case rhi::TextureFormat::RGBA16Float:return WGPUVertexFormat_Float16x4;
        case rhi::TextureFormat::R32Float:   return WGPUVertexFormat_Float32;
        case rhi::TextureFormat::RG32Float:  return WGPUVertexFormat_Float32x2;
        case rhi::TextureFormat::RGB32Float: return WGPUVertexFormat_Float32x3;
        case rhi::TextureFormat::RGBA32Float:return WGPUVertexFormat_Float32x4;
        case rhi::TextureFormat::R32Sint:    return WGPUVertexFormat_Sint32;
        case rhi::TextureFormat::RG32Sint:   return WGPUVertexFormat_Sint32x2;
        case rhi::TextureFormat::RGB32Sint:  return WGPUVertexFormat_Sint32x3;
        case rhi::TextureFormat::RGBA32Sint: return WGPUVertexFormat_Sint32x4;
        case rhi::TextureFormat::R32Uint:    return WGPUVertexFormat_Uint32;
        case rhi::TextureFormat::RG32Uint:   return WGPUVertexFormat_Uint32x2;
        case rhi::TextureFormat::RGB32Uint:  return WGPUVertexFormat_Uint32x3;
        case rhi::TextureFormat::RGBA32Uint: return WGPUVertexFormat_Uint32x4;
        default: throw std::runtime_error("Unsupported vertex format");
    }
}

} // namespace WebGPU
} // namespace RHI
