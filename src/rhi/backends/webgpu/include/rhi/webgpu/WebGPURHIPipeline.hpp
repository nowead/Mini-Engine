#pragma once

#include "WebGPUCommon.hpp"

namespace RHI {
namespace WebGPU {

// Forward declarations
class WebGPURHIDevice;

// Bring RHI types into scope
using rhi::RHIPipelineLayout;
using rhi::RHIRenderPipeline;
using rhi::RHIComputePipeline;
using rhi::PipelineLayoutDesc;
using rhi::RenderPipelineDesc;
using rhi::ComputePipelineDesc;

/**
 * @brief WebGPU implementation of RHIPipelineLayout
 *
 * Wraps WGPUPipelineLayout for defining bind group layouts.
 */
class WebGPURHIPipelineLayout : public RHIPipelineLayout {
public:
    WebGPURHIPipelineLayout(WebGPURHIDevice* device, const PipelineLayoutDesc& desc);
    ~WebGPURHIPipelineLayout() override;

    // Non-copyable, movable
    WebGPURHIPipelineLayout(const WebGPURHIPipelineLayout&) = delete;
    WebGPURHIPipelineLayout& operator=(const WebGPURHIPipelineLayout&) = delete;
    WebGPURHIPipelineLayout(WebGPURHIPipelineLayout&&) noexcept;
    WebGPURHIPipelineLayout& operator=(WebGPURHIPipelineLayout&&) noexcept;

    // WebGPU-specific accessors
    WGPUPipelineLayout getWGPUPipelineLayout() const { return m_pipelineLayout; }

private:
    WebGPURHIDevice* m_device;
    WGPUPipelineLayout m_pipelineLayout = nullptr;
};

/**
 * @brief WebGPU implementation of RHIRenderPipeline
 *
 * Wraps WGPURenderPipeline for graphics pipeline state.
 */
class WebGPURHIRenderPipeline : public RHIRenderPipeline {
public:
    WebGPURHIRenderPipeline(WebGPURHIDevice* device, const RenderPipelineDesc& desc);
    ~WebGPURHIRenderPipeline() override;

    // Non-copyable, movable
    WebGPURHIRenderPipeline(const WebGPURHIRenderPipeline&) = delete;
    WebGPURHIRenderPipeline& operator=(const WebGPURHIRenderPipeline&) = delete;
    WebGPURHIRenderPipeline(WebGPURHIRenderPipeline&&) noexcept;
    WebGPURHIRenderPipeline& operator=(WebGPURHIRenderPipeline&&) noexcept;

    // WebGPU-specific accessors
    WGPURenderPipeline getWGPURenderPipeline() const { return m_pipeline; }

private:
    WebGPURHIDevice* m_device;
    WGPURenderPipeline m_pipeline = nullptr;
};

/**
 * @brief WebGPU implementation of RHIComputePipeline
 *
 * Wraps WGPUComputePipeline for compute pipeline state.
 */
class WebGPURHIComputePipeline : public RHIComputePipeline {
public:
    WebGPURHIComputePipeline(WebGPURHIDevice* device, const ComputePipelineDesc& desc);
    ~WebGPURHIComputePipeline() override;

    // Non-copyable, movable
    WebGPURHIComputePipeline(const WebGPURHIComputePipeline&) = delete;
    WebGPURHIComputePipeline& operator=(const WebGPURHIComputePipeline&) = delete;
    WebGPURHIComputePipeline(WebGPURHIComputePipeline&&) noexcept;
    WebGPURHIComputePipeline& operator=(WebGPURHIComputePipeline&&) noexcept;

    // WebGPU-specific accessors
    WGPUComputePipeline getWGPUComputePipeline() const { return m_pipeline; }

private:
    WebGPURHIDevice* m_device;
    WGPUComputePipeline m_pipeline = nullptr;
};

} // namespace WebGPU
} // namespace RHI
