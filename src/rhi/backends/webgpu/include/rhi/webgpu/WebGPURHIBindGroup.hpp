#pragma once

#include "WebGPUCommon.hpp"

namespace RHI {
namespace WebGPU {

// Forward declarations
class WebGPURHIDevice;

// Bring RHI types into scope
using rhi::RHIBindGroupLayout;
using rhi::RHIBindGroup;
using rhi::BindGroupLayoutDesc;
using rhi::BindGroupDesc;

/**
 * @brief WebGPU implementation of RHIBindGroupLayout
 *
 * Wraps WGPUBindGroupLayout for describing bind group structure.
 */
class WebGPURHIBindGroupLayout : public RHIBindGroupLayout {
public:
    WebGPURHIBindGroupLayout(WebGPURHIDevice* device, const BindGroupLayoutDesc& desc);
    ~WebGPURHIBindGroupLayout() override;

    // Non-copyable, movable
    WebGPURHIBindGroupLayout(const WebGPURHIBindGroupLayout&) = delete;
    WebGPURHIBindGroupLayout& operator=(const WebGPURHIBindGroupLayout&) = delete;
    WebGPURHIBindGroupLayout(WebGPURHIBindGroupLayout&&) noexcept;
    WebGPURHIBindGroupLayout& operator=(WebGPURHIBindGroupLayout&&) noexcept;

    // WebGPU-specific accessors
    WGPUBindGroupLayout getWGPUBindGroupLayout() const { return m_bindGroupLayout; }

private:
    WebGPURHIDevice* m_device;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
};

/**
 * @brief WebGPU implementation of RHIBindGroup
 *
 * Wraps WGPUBindGroup for resource bindings.
 */
class WebGPURHIBindGroup : public RHIBindGroup {
public:
    WebGPURHIBindGroup(WebGPURHIDevice* device, const BindGroupDesc& desc);
    ~WebGPURHIBindGroup() override;

    // Non-copyable, movable
    WebGPURHIBindGroup(const WebGPURHIBindGroup&) = delete;
    WebGPURHIBindGroup& operator=(const WebGPURHIBindGroup&) = delete;
    WebGPURHIBindGroup(WebGPURHIBindGroup&&) noexcept;
    WebGPURHIBindGroup& operator=(WebGPURHIBindGroup&&) noexcept;

    // WebGPU-specific accessors
    WGPUBindGroup getWGPUBindGroup() const { return m_bindGroup; }

private:
    WebGPURHIDevice* m_device;
    WGPUBindGroup m_bindGroup = nullptr;
};

} // namespace WebGPU
} // namespace RHI
