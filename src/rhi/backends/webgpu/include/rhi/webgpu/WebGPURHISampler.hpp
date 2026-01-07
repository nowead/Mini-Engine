#pragma once

#include "WebGPUCommon.hpp"

namespace RHI {
namespace WebGPU {

// Forward declarations
class WebGPURHIDevice;

// Bring RHI types into scope
using rhi::RHISampler;
using rhi::SamplerDesc;

/**
 * @brief WebGPU implementation of RHISampler
 *
 * Wraps WGPUSampler for texture sampling configuration.
 * Samplers are immutable state objects created from SamplerDesc.
 */
class WebGPURHISampler : public RHISampler {
public:
    /**
     * @brief Create sampler from descriptor
     */
    WebGPURHISampler(WebGPURHIDevice* device, const SamplerDesc& desc);
    ~WebGPURHISampler() override;

    // Non-copyable, movable
    WebGPURHISampler(const WebGPURHISampler&) = delete;
    WebGPURHISampler& operator=(const WebGPURHISampler&) = delete;
    WebGPURHISampler(WebGPURHISampler&&) noexcept;
    WebGPURHISampler& operator=(WebGPURHISampler&&) noexcept;

    // WebGPU-specific accessors
    WGPUSampler getWGPUSampler() const { return m_sampler; }

private:
    WebGPURHIDevice* m_device;
    WGPUSampler m_sampler = nullptr;
};

} // namespace WebGPU
} // namespace RHI
