#pragma once

#include "RHITypes.hpp"
#include <cstdint>

namespace rhi {

/**
 * @brief Sampler creation descriptor
 */
struct SamplerDesc {
    // Filtering
    FilterMode magFilter = FilterMode::Linear;      // Magnification filter
    FilterMode minFilter = FilterMode::Linear;      // Minification filter
    MipmapMode mipmapFilter = MipmapMode::Linear;   // Mipmap filter

    // Address modes
    AddressMode addressModeU = AddressMode::Repeat;  // U (or S) coordinate addressing
    AddressMode addressModeV = AddressMode::Repeat;  // V (or T) coordinate addressing
    AddressMode addressModeW = AddressMode::Repeat;  // W (or R) coordinate addressing

    // LOD (Level of Detail)
    float lodMinClamp = 0.0f;       // Minimum LOD clamp
    float lodMaxClamp = 1000.0f;    // Maximum LOD clamp
    float mipLodBias = 0.0f;        // LOD bias

    // Anisotropic filtering
    bool anisotropyEnable = false;
    float maxAnisotropy = 1.0f;     // Maximum anisotropy (typically 1, 2, 4, 8, or 16)

    // Comparison (for shadow sampling)
    bool compareEnable = false;
    CompareOp compareOp = CompareOp::Never;

    // Border color (for ClampToBorder address mode)
    ClearColorValue borderColor;

    // Debug label
    const char* label = nullptr;

    SamplerDesc() = default;
};

/**
 * @brief Sampler interface for texture sampling configuration
 *
 * Samplers define how textures are sampled in shaders, including filtering,
 * addressing modes, and LOD behavior.
 */
class RHISampler {
public:
    virtual ~RHISampler() = default;

    // Samplers are immutable state objects with no methods after creation
    // They are used in bind groups to configure texture sampling
};

} // namespace rhi
