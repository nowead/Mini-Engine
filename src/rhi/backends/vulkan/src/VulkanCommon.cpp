#include <rhi/vulkan/VulkanCommon.hpp>

namespace RHI {
namespace Vulkan {

// Bring RHI types into scope
using rhi::TextureFormat;
using rhi::BufferUsage;
using rhi::TextureUsage;
using rhi::ShaderStage;
using rhi::PrimitiveTopology;
using rhi::CompareOp;
using rhi::BlendFactor;
using rhi::BlendOp;

// ============================================================================
// Format Conversion
// ============================================================================

vk::Format ToVkFormat(TextureFormat format) {
    switch (format) {
        // 8-bit formats
        case TextureFormat::R8Unorm:            return vk::Format::eR8Unorm;
        case TextureFormat::R8Snorm:            return vk::Format::eR8Snorm;
        case TextureFormat::R8Uint:             return vk::Format::eR8Uint;
        case TextureFormat::R8Sint:             return vk::Format::eR8Sint;

        // 16-bit formats
        case TextureFormat::R16Uint:            return vk::Format::eR16Uint;
        case TextureFormat::R16Sint:            return vk::Format::eR16Sint;
        case TextureFormat::R16Float:           return vk::Format::eR16Sfloat;
        case TextureFormat::RG8Unorm:           return vk::Format::eR8G8Unorm;
        case TextureFormat::RG8Snorm:           return vk::Format::eR8G8Snorm;
        case TextureFormat::RG8Uint:            return vk::Format::eR8G8Uint;
        case TextureFormat::RG8Sint:            return vk::Format::eR8G8Sint;

        // 32-bit formats
        case TextureFormat::R32Uint:            return vk::Format::eR32Uint;
        case TextureFormat::R32Sint:            return vk::Format::eR32Sint;
        case TextureFormat::R32Float:           return vk::Format::eR32Sfloat;
        case TextureFormat::RG16Uint:           return vk::Format::eR16G16Uint;
        case TextureFormat::RG16Sint:           return vk::Format::eR16G16Sint;
        case TextureFormat::RG16Float:          return vk::Format::eR16G16Sfloat;
        case TextureFormat::RGBA8Unorm:         return vk::Format::eR8G8B8A8Unorm;
        case TextureFormat::RGBA8UnormSrgb:     return vk::Format::eR8G8B8A8Srgb;
        case TextureFormat::RGBA8Snorm:         return vk::Format::eR8G8B8A8Snorm;
        case TextureFormat::RGBA8Uint:          return vk::Format::eR8G8B8A8Uint;
        case TextureFormat::RGBA8Sint:          return vk::Format::eR8G8B8A8Sint;
        case TextureFormat::BGRA8Unorm:         return vk::Format::eB8G8R8A8Unorm;
        case TextureFormat::BGRA8UnormSrgb:     return vk::Format::eB8G8R8A8Srgb;

        // 64-bit formats
        case TextureFormat::RG32Uint:           return vk::Format::eR32G32Uint;
        case TextureFormat::RG32Sint:           return vk::Format::eR32G32Sint;
        case TextureFormat::RG32Float:          return vk::Format::eR32G32Sfloat;
        case TextureFormat::RGBA16Uint:         return vk::Format::eR16G16B16A16Uint;
        case TextureFormat::RGBA16Sint:         return vk::Format::eR16G16B16A16Sint;
        case TextureFormat::RGBA16Float:        return vk::Format::eR16G16B16A16Sfloat;

        // 96-bit formats (for vertex attributes)
        case TextureFormat::RGB32Uint:          return vk::Format::eR32G32B32Uint;
        case TextureFormat::RGB32Sint:          return vk::Format::eR32G32B32Sint;
        case TextureFormat::RGB32Float:         return vk::Format::eR32G32B32Sfloat;

        // 128-bit formats
        case TextureFormat::RGBA32Uint:         return vk::Format::eR32G32B32A32Uint;
        case TextureFormat::RGBA32Sint:         return vk::Format::eR32G32B32A32Sint;
        case TextureFormat::RGBA32Float:        return vk::Format::eR32G32B32A32Sfloat;

        // Depth/stencil formats
        case TextureFormat::Depth16Unorm:       return vk::Format::eD16Unorm;
        case TextureFormat::Depth32Float:       return vk::Format::eD32Sfloat;
        case TextureFormat::Depth24Plus:        return vk::Format::eD24UnormS8Uint;
        case TextureFormat::Depth24PlusStencil8:return vk::Format::eD24UnormS8Uint;

        default:
            throw std::runtime_error("Unsupported texture format");
    }
}

TextureFormat FromVkFormat(vk::Format format) {
    // Reverse mapping (implement as needed)
    switch (format) {
        case vk::Format::eR8G8B8A8Unorm:    return TextureFormat::RGBA8Unorm;
        case vk::Format::eB8G8R8A8Unorm:    return TextureFormat::BGRA8Unorm;
        case vk::Format::eR8G8B8A8Srgb:     return TextureFormat::RGBA8UnormSrgb;
        case vk::Format::eD32Sfloat:        return TextureFormat::Depth32Float;
        case vk::Format::eD24UnormS8Uint:   return TextureFormat::Depth24PlusStencil8;
        default:
            throw std::runtime_error("Unsupported Vulkan format");
    }
}

// ============================================================================
// Buffer Usage Conversion
// ============================================================================

vk::BufferUsageFlags ToVkBufferUsage(BufferUsage usage) {
    vk::BufferUsageFlags flags = {};

    if (hasFlag(usage, BufferUsage::Vertex))
        flags |= vk::BufferUsageFlagBits::eVertexBuffer;
    if (hasFlag(usage, BufferUsage::Index))
        flags |= vk::BufferUsageFlagBits::eIndexBuffer;
    if (hasFlag(usage, BufferUsage::Uniform))
        flags |= vk::BufferUsageFlagBits::eUniformBuffer;
    if (hasFlag(usage, BufferUsage::Storage))
        flags |= vk::BufferUsageFlagBits::eStorageBuffer;
    if (hasFlag(usage, BufferUsage::Indirect))
        flags |= vk::BufferUsageFlagBits::eIndirectBuffer;
    if (hasFlag(usage, BufferUsage::CopySrc))
        flags |= vk::BufferUsageFlagBits::eTransferSrc;
    if (hasFlag(usage, BufferUsage::CopyDst))
        flags |= vk::BufferUsageFlagBits::eTransferDst;

    return flags;
}

// ============================================================================
// Texture/Image Usage Conversion
// ============================================================================

vk::ImageUsageFlags ToVkImageUsage(TextureUsage usage) {
    vk::ImageUsageFlags flags = {};

    if (hasFlag(usage, TextureUsage::CopySrc))
        flags |= vk::ImageUsageFlagBits::eTransferSrc;
    if (hasFlag(usage, TextureUsage::CopyDst))
        flags |= vk::ImageUsageFlagBits::eTransferDst;
    if (hasFlag(usage, TextureUsage::Sampled))
        flags |= vk::ImageUsageFlagBits::eSampled;
    if (hasFlag(usage, TextureUsage::Storage))
        flags |= vk::ImageUsageFlagBits::eStorage;
    if (hasFlag(usage, TextureUsage::RenderTarget))
        flags |= vk::ImageUsageFlagBits::eColorAttachment;
    if (hasFlag(usage, TextureUsage::DepthStencil))
        flags |= vk::ImageUsageFlagBits::eDepthStencilAttachment;

    return flags;
}

// ============================================================================
// Shader Stage Conversion
// ============================================================================

vk::ShaderStageFlags ToVkShaderStage(ShaderStage stage) {
    vk::ShaderStageFlags flags = {};

    if (hasFlag(stage, ShaderStage::Vertex))
        flags |= vk::ShaderStageFlagBits::eVertex;
    if (hasFlag(stage, ShaderStage::Fragment))
        flags |= vk::ShaderStageFlagBits::eFragment;
    if (hasFlag(stage, ShaderStage::Compute))
        flags |= vk::ShaderStageFlagBits::eCompute;

    return flags;
}

// ============================================================================
// Pipeline State Conversion
// ============================================================================

vk::PrimitiveTopology ToVkPrimitiveTopology(PrimitiveTopology topology) {
    switch (topology) {
        case PrimitiveTopology::PointList:      return vk::PrimitiveTopology::ePointList;
        case PrimitiveTopology::LineList:       return vk::PrimitiveTopology::eLineList;
        case PrimitiveTopology::LineStrip:      return vk::PrimitiveTopology::eLineStrip;
        case PrimitiveTopology::TriangleList:   return vk::PrimitiveTopology::eTriangleList;
        case PrimitiveTopology::TriangleStrip:  return vk::PrimitiveTopology::eTriangleStrip;
        default:
            throw std::runtime_error("Unsupported primitive topology");
    }
}

vk::CompareOp ToVkCompareOp(CompareOp func) {
    switch (func) {
        case CompareOp::Never:            return vk::CompareOp::eNever;
        case CompareOp::Less:             return vk::CompareOp::eLess;
        case CompareOp::Equal:            return vk::CompareOp::eEqual;
        case CompareOp::LessOrEqual:      return vk::CompareOp::eLessOrEqual;
        case CompareOp::Greater:          return vk::CompareOp::eGreater;
        case CompareOp::NotEqual:         return vk::CompareOp::eNotEqual;
        case CompareOp::GreaterOrEqual:   return vk::CompareOp::eGreaterOrEqual;
        case CompareOp::Always:           return vk::CompareOp::eAlways;
        default:
            throw std::runtime_error("Unsupported compare function");
    }
}

vk::BlendFactor ToVkBlendFactor(BlendFactor factor) {
    switch (factor) {
        case BlendFactor::Zero:                 return vk::BlendFactor::eZero;
        case BlendFactor::One:                  return vk::BlendFactor::eOne;
        case BlendFactor::SrcColor:             return vk::BlendFactor::eSrcColor;
        case BlendFactor::OneMinusSrcColor:     return vk::BlendFactor::eOneMinusSrcColor;
        case BlendFactor::DstColor:             return vk::BlendFactor::eDstColor;
        case BlendFactor::OneMinusDstColor:     return vk::BlendFactor::eOneMinusDstColor;
        case BlendFactor::SrcAlpha:             return vk::BlendFactor::eSrcAlpha;
        case BlendFactor::OneMinusSrcAlpha:     return vk::BlendFactor::eOneMinusSrcAlpha;
        case BlendFactor::DstAlpha:             return vk::BlendFactor::eDstAlpha;
        case BlendFactor::OneMinusDstAlpha:     return vk::BlendFactor::eOneMinusDstAlpha;
        case BlendFactor::ConstantColor:        return vk::BlendFactor::eConstantColor;
        case BlendFactor::OneMinusConstantColor:return vk::BlendFactor::eOneMinusConstantColor;
        case BlendFactor::ConstantAlpha:        return vk::BlendFactor::eConstantAlpha;
        case BlendFactor::OneMinusConstantAlpha:return vk::BlendFactor::eOneMinusConstantAlpha;
        default:
            throw std::runtime_error("Unsupported blend factor");
    }
}

vk::BlendOp ToVkBlendOp(BlendOp op) {
    switch (op) {
        case BlendOp::Add:               return vk::BlendOp::eAdd;
        case BlendOp::Subtract:          return vk::BlendOp::eSubtract;
        case BlendOp::ReverseSubtract:   return vk::BlendOp::eReverseSubtract;
        case BlendOp::Min:               return vk::BlendOp::eMin;
        case BlendOp::Max:               return vk::BlendOp::eMax;
        default:
            throw std::runtime_error("Unsupported blend operation");
    }
}

// ============================================================================
// Sampler Conversion
// ============================================================================

vk::Filter ToVkFilter(rhi::FilterMode mode) {
    switch (mode) {
        case rhi::FilterMode::Nearest:   return vk::Filter::eNearest;
        case rhi::FilterMode::Linear:    return vk::Filter::eLinear;
        default:
            throw std::runtime_error("Unsupported filter mode");
    }
}

vk::SamplerMipmapMode ToVkSamplerMipmapMode(rhi::MipmapMode mode) {
    switch (mode) {
        case rhi::MipmapMode::Nearest:   return vk::SamplerMipmapMode::eNearest;
        case rhi::MipmapMode::Linear:    return vk::SamplerMipmapMode::eLinear;
        default:
            throw std::runtime_error("Unsupported mipmap mode");
    }
}

vk::SamplerAddressMode ToVkSamplerAddressMode(rhi::AddressMode mode) {
    switch (mode) {
        case rhi::AddressMode::Repeat:          return vk::SamplerAddressMode::eRepeat;
        case rhi::AddressMode::MirrorRepeat:    return vk::SamplerAddressMode::eMirroredRepeat;
        case rhi::AddressMode::ClampToEdge:     return vk::SamplerAddressMode::eClampToEdge;
        case rhi::AddressMode::ClampToBorder:   return vk::SamplerAddressMode::eClampToBorder;
        default:
            throw std::runtime_error("Unsupported address mode");
    }
}

// ============================================================================
// Rasterization State Conversion
// ============================================================================

vk::PolygonMode ToVkPolygonMode(rhi::PolygonMode mode) {
    switch (mode) {
        case rhi::PolygonMode::Fill:    return vk::PolygonMode::eFill;
        case rhi::PolygonMode::Line:    return vk::PolygonMode::eLine;
        case rhi::PolygonMode::Point:   return vk::PolygonMode::ePoint;
        default:
            throw std::runtime_error("Unsupported polygon mode");
    }
}

vk::CullModeFlags ToVkCullMode(rhi::CullMode mode) {
    switch (mode) {
        case rhi::CullMode::None:   return vk::CullModeFlagBits::eNone;
        case rhi::CullMode::Front:  return vk::CullModeFlagBits::eFront;
        case rhi::CullMode::Back:   return vk::CullModeFlagBits::eBack;
        default:
            throw std::runtime_error("Unsupported cull mode");
    }
}

vk::FrontFace ToVkFrontFace(rhi::FrontFace face) {
    switch (face) {
        case rhi::FrontFace::CounterClockwise:  return vk::FrontFace::eCounterClockwise;
        case rhi::FrontFace::Clockwise:         return vk::FrontFace::eClockwise;
        default:
            throw std::runtime_error("Unsupported front face");
    }
}

vk::ColorComponentFlags ToVkColorComponentFlags(rhi::ColorWriteMask mask) {
    vk::ColorComponentFlags flags = {};

    uint32_t maskValue = static_cast<uint32_t>(mask);

    if (maskValue & static_cast<uint32_t>(rhi::ColorWriteMask::Red))
        flags |= vk::ColorComponentFlagBits::eR;
    if (maskValue & static_cast<uint32_t>(rhi::ColorWriteMask::Green))
        flags |= vk::ColorComponentFlagBits::eG;
    if (maskValue & static_cast<uint32_t>(rhi::ColorWriteMask::Blue))
        flags |= vk::ColorComponentFlagBits::eB;
    if (maskValue & static_cast<uint32_t>(rhi::ColorWriteMask::Alpha))
        flags |= vk::ColorComponentFlagBits::eA;

    return flags;
}

// ============================================================================
// Render Pass Conversion
// ============================================================================

vk::AttachmentLoadOp ToVkAttachmentLoadOp(rhi::LoadOp op) {
    switch (op) {
        case rhi::LoadOp::Load:     return vk::AttachmentLoadOp::eLoad;
        case rhi::LoadOp::Clear:    return vk::AttachmentLoadOp::eClear;
        case rhi::LoadOp::DontCare: return vk::AttachmentLoadOp::eDontCare;
        default:
            throw std::runtime_error("Unsupported load op");
    }
}

vk::AttachmentStoreOp ToVkAttachmentStoreOp(rhi::StoreOp op) {
    switch (op) {
        case rhi::StoreOp::Store:    return vk::AttachmentStoreOp::eStore;
        case rhi::StoreOp::DontCare: return vk::AttachmentStoreOp::eDontCare;
        default:
            throw std::runtime_error("Unsupported store op");
    }
}

} // namespace Vulkan
} // namespace RHI
