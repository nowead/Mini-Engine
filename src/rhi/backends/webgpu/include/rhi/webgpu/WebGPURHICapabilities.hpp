#pragma once

#include "WebGPUCommon.hpp"

namespace RHI {
namespace WebGPU {

// Forward declarations
class WebGPURHIDevice;

// Bring RHI types into scope
using rhi::RHICapabilities;
using rhi::RHILimits;
using rhi::RHIFeatures;
using rhi::TextureFormat;
using rhi::TextureUsage;

/**
 * @brief WebGPU implementation of RHICapabilities
 *
 * Queries WebGPU device limits and features.
 */
class WebGPURHICapabilities : public RHICapabilities {
public:
    WebGPURHICapabilities(WebGPURHIDevice* device);
    ~WebGPURHICapabilities() override;

    // RHICapabilities interface
    const RHILimits& getLimits() const override { return m_limits; }
    const RHIFeatures& getFeatures() const override { return m_features; }
    bool isFormatSupported(TextureFormat format, TextureUsage usage) const override;
    bool isSampleCountSupported(TextureFormat format, uint32_t sampleCount) const override;

private:
    void queryLimits(WGPUDevice device);
    void queryFeatures(WGPUDevice device);

    WebGPURHIDevice* m_device;
    RHILimits m_limits;
    RHIFeatures m_features;
};

} // namespace WebGPU
} // namespace RHI
