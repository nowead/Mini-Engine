#include <rhi-webgpu/WebGPURHIPipeline.hpp>
#include <rhi-webgpu/WebGPURHIDevice.hpp>
#include <rhi-webgpu/WebGPURHIShader.hpp>
#include <rhi-webgpu/WebGPURHIBindGroup.hpp>
#include <stdexcept>
#include <vector>

namespace RHI {
namespace WebGPU {

// ============================================================================
// WebGPURHIPipelineLayout Implementation
// ============================================================================

WebGPURHIPipelineLayout::WebGPURHIPipelineLayout(WebGPURHIDevice* device,
                                                 const PipelineLayoutDesc& desc)
    : m_device(device)
{
    std::vector<WGPUBindGroupLayout> wgpuLayouts;
    wgpuLayouts.reserve(desc.bindGroupLayouts.size());

    for (auto* layout : desc.bindGroupLayouts) {
        auto* webgpuLayout = static_cast<WebGPURHIBindGroupLayout*>(layout);
        wgpuLayouts.push_back(webgpuLayout->getWGPUBindGroupLayout());
    }

    WGPUPipelineLayoutDescriptor layoutDesc{};
    layoutDesc.label = desc.label;
    layoutDesc.bindGroupLayoutCount = static_cast<uint32_t>(wgpuLayouts.size());
    layoutDesc.bindGroupLayouts = wgpuLayouts.data();

    m_pipelineLayout = wgpuDeviceCreatePipelineLayout(m_device->getWGPUDevice(), &layoutDesc);
    if (!m_pipelineLayout) {
        throw std::runtime_error("Failed to create WebGPU pipeline layout");
    }
}

WebGPURHIPipelineLayout::~WebGPURHIPipelineLayout() {
    if (m_pipelineLayout) {
        wgpuPipelineLayoutRelease(m_pipelineLayout);
        m_pipelineLayout = nullptr;
    }
}

WebGPURHIPipelineLayout::WebGPURHIPipelineLayout(WebGPURHIPipelineLayout&& other) noexcept
    : m_device(other.m_device)
    , m_pipelineLayout(other.m_pipelineLayout)
{
    other.m_pipelineLayout = nullptr;
}

WebGPURHIPipelineLayout& WebGPURHIPipelineLayout::operator=(WebGPURHIPipelineLayout&& other) noexcept {
    if (this != &other) {
        if (m_pipelineLayout) {
            wgpuPipelineLayoutRelease(m_pipelineLayout);
        }

        m_device = other.m_device;
        m_pipelineLayout = other.m_pipelineLayout;

        other.m_pipelineLayout = nullptr;
    }
    return *this;
}

// ============================================================================
// WebGPURHIRenderPipeline Implementation
// ============================================================================

WebGPURHIRenderPipeline::WebGPURHIRenderPipeline(WebGPURHIDevice* device,
                                                 const RenderPipelineDesc& desc)
    : m_device(device)
{
    auto* webgpuLayout = static_cast<WebGPURHIPipelineLayout*>(desc.layout);
    auto* vertexShader = static_cast<WebGPURHIShader*>(desc.vertexShader);
    auto* fragmentShader = static_cast<WebGPURHIShader*>(desc.fragmentShader);

    // Vertex buffer layouts
    std::vector<WGPUVertexBufferLayout> vertexBuffers;
    std::vector<std::vector<WGPUVertexAttribute>> attributesPerBuffer;

    for (size_t i = 0; i < desc.vertex.buffers.size(); ++i) {
        const auto& bufferLayout = desc.vertex.buffers[i];

        // Create attributes for this buffer
        std::vector<WGPUVertexAttribute> attributes;
        for (const auto& attr : bufferLayout.attributes) {
            WGPUVertexAttribute wgpuAttr{};
            wgpuAttr.format = ToWGPUVertexFormat(attr.format);
            wgpuAttr.offset = attr.offset;
            wgpuAttr.shaderLocation = attr.location;
            attributes.push_back(wgpuAttr);
        }

        attributesPerBuffer.push_back(std::move(attributes));

        WGPUVertexBufferLayout vbLayout{};
        vbLayout.arrayStride = bufferLayout.stride;
        vbLayout.stepMode = (bufferLayout.inputRate == rhi::VertexInputRate::Instance)
            ? WGPUVertexStepMode_Instance
            : WGPUVertexStepMode_Vertex;
        vbLayout.attributeCount = static_cast<uint32_t>(attributesPerBuffer[i].size());
        vbLayout.attributes = attributesPerBuffer[i].data();

        vertexBuffers.push_back(vbLayout);
    }

    // Vertex state
    WGPUVertexState vertexState{};
    vertexState.module = vertexShader->getWGPUShaderModule();
    vertexState.entryPoint = vertexShader->getEntryPoint().c_str();
    vertexState.bufferCount = static_cast<uint32_t>(vertexBuffers.size());
    vertexState.buffers = vertexBuffers.data();

    // Primitive state
    WGPUPrimitiveState primitiveState{};
    primitiveState.topology = ToWGPUTopology(desc.primitive.topology);
    primitiveState.frontFace = ToWGPUFrontFace(desc.primitive.frontFace);
    primitiveState.cullMode = ToWGPUCullMode(desc.primitive.cullMode);

    // Strip index format (only for strip topologies)
    if (desc.primitive.topology == rhi::PrimitiveTopology::TriangleStrip ||
        desc.primitive.topology == rhi::PrimitiveTopology::LineStrip) {
        primitiveState.stripIndexFormat = ToWGPUIndexFormat(desc.primitive.indexFormat);
    }

    // Depth-stencil state
    WGPUDepthStencilState depthStencilState{};
    WGPUDepthStencilState* pDepthStencil = nullptr;
    if (desc.depthStencil) {
        depthStencilState.format = ToWGPUFormat(desc.depthStencil->format);
        depthStencilState.depthWriteEnabled = desc.depthStencil->depthWriteEnabled;
        depthStencilState.depthCompare = ToWGPUCompareFunc(desc.depthStencil->depthCompare);

        // Stencil (simplified - full stencil state can be added if needed)
        depthStencilState.stencilFront.compare = WGPUCompareFunction_Always;
        depthStencilState.stencilFront.failOp = WGPUStencilOperation_Keep;
        depthStencilState.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
        depthStencilState.stencilFront.passOp = WGPUStencilOperation_Keep;

        depthStencilState.stencilBack = depthStencilState.stencilFront;

        depthStencilState.stencilReadMask = 0xFFFFFFFF;
        depthStencilState.stencilWriteMask = 0xFFFFFFFF;

        depthStencilState.depthBias = 0;
        depthStencilState.depthBiasSlopeScale = 0.0f;
        depthStencilState.depthBiasClamp = 0.0f;

        pDepthStencil = &depthStencilState;
    }

    // Color targets
    std::vector<WGPUColorTargetState> colorTargets;
    std::vector<WGPUBlendState> blendStates;

    for (const auto& target : desc.colorTargets) {
        WGPUBlendState blendState{};
        if (target.blend.blendEnabled) {
            blendState.color.operation = ToWGPUBlendOp(target.blend.colorBlendOp);
            blendState.color.srcFactor = ToWGPUBlendFactor(target.blend.srcColorFactor);
            blendState.color.dstFactor = ToWGPUBlendFactor(target.blend.dstColorFactor);

            blendState.alpha.operation = ToWGPUBlendOp(target.blend.alphaBlendOp);
            blendState.alpha.srcFactor = ToWGPUBlendFactor(target.blend.srcAlphaFactor);
            blendState.alpha.dstFactor = ToWGPUBlendFactor(target.blend.dstAlphaFactor);

            blendStates.push_back(blendState);
        }

        WGPUColorTargetState colorTarget{};
        colorTarget.format = ToWGPUFormat(target.format);
        colorTarget.blend = target.blend.blendEnabled ? &blendStates.back() : nullptr;
        colorTarget.writeMask = ToWGPUColorWriteMask(target.blend.writeMask);

        colorTargets.push_back(colorTarget);
    }

    // Fragment state
    WGPUFragmentState fragmentState{};
    WGPUFragmentState* pFragment = nullptr;
    if (fragmentShader) {
        fragmentState.module = fragmentShader->getWGPUShaderModule();
        fragmentState.entryPoint = fragmentShader->getEntryPoint().c_str();
        fragmentState.targetCount = static_cast<uint32_t>(colorTargets.size());
        fragmentState.targets = colorTargets.data();
        pFragment = &fragmentState;
    }

    // Multisample state
    WGPUMultisampleState multisampleState{};
    multisampleState.count = desc.multisample.sampleCount;
    multisampleState.mask = desc.multisample.sampleMask;
    multisampleState.alphaToCoverageEnabled = desc.multisample.alphaToCoverageEnabled;

    // Create render pipeline
    WGPURenderPipelineDescriptor pipelineDesc{};
    pipelineDesc.label = desc.label;
    pipelineDesc.layout = webgpuLayout->getWGPUPipelineLayout();
    pipelineDesc.vertex = vertexState;
    pipelineDesc.primitive = primitiveState;
    pipelineDesc.depthStencil = pDepthStencil;
    pipelineDesc.multisample = multisampleState;
    pipelineDesc.fragment = pFragment;

    m_pipeline = wgpuDeviceCreateRenderPipeline(m_device->getWGPUDevice(), &pipelineDesc);
    if (!m_pipeline) {
        throw std::runtime_error("Failed to create WebGPU render pipeline");
    }
}

WebGPURHIRenderPipeline::~WebGPURHIRenderPipeline() {
    if (m_pipeline) {
        wgpuRenderPipelineRelease(m_pipeline);
        m_pipeline = nullptr;
    }
}

WebGPURHIRenderPipeline::WebGPURHIRenderPipeline(WebGPURHIRenderPipeline&& other) noexcept
    : m_device(other.m_device)
    , m_pipeline(other.m_pipeline)
{
    other.m_pipeline = nullptr;
}

WebGPURHIRenderPipeline& WebGPURHIRenderPipeline::operator=(WebGPURHIRenderPipeline&& other) noexcept {
    if (this != &other) {
        if (m_pipeline) {
            wgpuRenderPipelineRelease(m_pipeline);
        }

        m_device = other.m_device;
        m_pipeline = other.m_pipeline;

        other.m_pipeline = nullptr;
    }
    return *this;
}

// ============================================================================
// WebGPURHIComputePipeline Implementation
// ============================================================================

WebGPURHIComputePipeline::WebGPURHIComputePipeline(WebGPURHIDevice* device,
                                                   const ComputePipelineDesc& desc)
    : m_device(device)
{
    auto* webgpuLayout = static_cast<WebGPURHIPipelineLayout*>(desc.layout);
    auto* computeShader = static_cast<WebGPURHIShader*>(desc.computeShader);

    WGPUComputePipelineDescriptor pipelineDesc{};
    pipelineDesc.label = desc.label;
    pipelineDesc.layout = webgpuLayout->getWGPUPipelineLayout();
    pipelineDesc.compute.module = computeShader->getWGPUShaderModule();
    pipelineDesc.compute.entryPoint = computeShader->getEntryPoint().c_str();

    m_pipeline = wgpuDeviceCreateComputePipeline(m_device->getWGPUDevice(), &pipelineDesc);
    if (!m_pipeline) {
        throw std::runtime_error("Failed to create WebGPU compute pipeline");
    }
}

WebGPURHIComputePipeline::~WebGPURHIComputePipeline() {
    if (m_pipeline) {
        wgpuComputePipelineRelease(m_pipeline);
        m_pipeline = nullptr;
    }
}

WebGPURHIComputePipeline::WebGPURHIComputePipeline(WebGPURHIComputePipeline&& other) noexcept
    : m_device(other.m_device)
    , m_pipeline(other.m_pipeline)
{
    other.m_pipeline = nullptr;
}

WebGPURHIComputePipeline& WebGPURHIComputePipeline::operator=(WebGPURHIComputePipeline&& other) noexcept {
    if (this != &other) {
        if (m_pipeline) {
            wgpuComputePipelineRelease(m_pipeline);
        }

        m_device = other.m_device;
        m_pipeline = other.m_pipeline;

        other.m_pipeline = nullptr;
    }
    return *this;
}

} // namespace WebGPU
} // namespace RHI
