#include <rhi/vulkan/VulkanRHIPipeline.hpp>
#include <rhi/vulkan/VulkanRHIDevice.hpp>
#include <rhi/vulkan/VulkanRHIShader.hpp>
#include <rhi/vulkan/VulkanRHIBindGroup.hpp>

namespace RHI {
namespace Vulkan {

// ============================================================================
// VulkanRHIPipelineLayout Implementation
// ============================================================================

VulkanRHIPipelineLayout::VulkanRHIPipelineLayout(VulkanRHIDevice* device, const PipelineLayoutDesc& desc)
    : m_device(device)
    , m_layout(nullptr)
{
    std::vector<vk::DescriptorSetLayout> setLayouts;
    setLayouts.reserve(desc.bindGroupLayouts.size());

    for (auto* layout : desc.bindGroupLayouts) {
        auto* vulkanLayout = static_cast<VulkanRHIBindGroupLayout*>(layout);
        setLayouts.push_back(vulkanLayout->getVkDescriptorSetLayout());
    }

    vk::PipelineLayoutCreateInfo layoutInfo;
    layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    layoutInfo.pSetLayouts = setLayouts.data();
    layoutInfo.pushConstantRangeCount = 0;  // TODO: Support push constants
    layoutInfo.pPushConstantRanges = nullptr;

    m_layout = vk::raii::PipelineLayout(m_device->getVkDevice(), layoutInfo);
}

VulkanRHIPipelineLayout::~VulkanRHIPipelineLayout() {
    // RAII handles cleanup automatically
}

VulkanRHIPipelineLayout::VulkanRHIPipelineLayout(VulkanRHIPipelineLayout&& other) noexcept
    : m_device(other.m_device)
    , m_layout(std::move(other.m_layout))
{
}

VulkanRHIPipelineLayout& VulkanRHIPipelineLayout::operator=(VulkanRHIPipelineLayout&& other) noexcept {
    if (this != &other) {
        m_device = other.m_device;
        m_layout = std::move(other.m_layout);
    }
    return *this;
}

// ============================================================================
// VulkanRHIRenderPipeline Implementation
// ============================================================================

VulkanRHIRenderPipeline::VulkanRHIRenderPipeline(VulkanRHIDevice* device, const RenderPipelineDesc& desc)
    : m_device(device)
    , m_pipeline(nullptr)
    , m_layout(desc.layout)  // Phase 7.5: Store layout for descriptor set binding
{
    if (!desc.vertexShader || !desc.fragmentShader) {
        throw std::runtime_error("Both vertex and fragment shaders are required");
    }
    if (!desc.layout) {
        throw std::runtime_error("Pipeline layout is required");
    }

    auto* vulkanVertexShader = static_cast<VulkanRHIShader*>(desc.vertexShader);
    auto* vulkanFragmentShader = static_cast<VulkanRHIShader*>(desc.fragmentShader);
    auto* vulkanLayout = static_cast<VulkanRHIPipelineLayout*>(desc.layout);

    // Shader stages
    std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;

    vk::PipelineShaderStageCreateInfo vertStage;
    vertStage.stage = vk::ShaderStageFlagBits::eVertex;
    vertStage.module = vulkanVertexShader->getVkShaderModule();
    vertStage.pName = vulkanVertexShader->getEntryPoint().c_str();
    shaderStages.push_back(vertStage);

    vk::PipelineShaderStageCreateInfo fragStage;
    fragStage.stage = vk::ShaderStageFlagBits::eFragment;
    fragStage.module = vulkanFragmentShader->getVkShaderModule();
    fragStage.pName = vulkanFragmentShader->getEntryPoint().c_str();
    shaderStages.push_back(fragStage);

    // Vertex input state
    std::vector<vk::VertexInputBindingDescription> bindings;
    std::vector<vk::VertexInputAttributeDescription> attributes;

    for (uint32_t i = 0; i < desc.vertex.buffers.size(); i++) {
        const auto& buffer = desc.vertex.buffers[i];

        vk::VertexInputBindingDescription binding;
        binding.binding = i;
        binding.stride = static_cast<uint32_t>(buffer.stride);
        binding.inputRate = buffer.inputRate == rhi::VertexInputRate::Instance
            ? vk::VertexInputRate::eInstance
            : vk::VertexInputRate::eVertex;
        bindings.push_back(binding);

        for (const auto& attr : buffer.attributes) {
            vk::VertexInputAttributeDescription attribute;
            attribute.location = attr.location;
            attribute.binding = attr.binding;
            attribute.format = ToVkFormat(attr.format);
            attribute.offset = static_cast<uint32_t>(attr.offset);
            attributes.push_back(attribute);
        }
    }

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertexInputInfo.pVertexBindingDescriptions = bindings.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributes.data();

    // Input assembly state
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly;
    inputAssembly.topology = ToVkPrimitiveTopology(desc.primitive.topology);
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport state (dynamic)
    vk::PipelineViewportStateCreateInfo viewportState;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterization state
    vk::PipelineRasterizationStateCreateInfo rasterizer;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = ToVkPolygonMode(desc.primitive.polygonMode);
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = ToVkCullMode(desc.primitive.cullMode);
    rasterizer.frontFace = ToVkFrontFace(desc.primitive.frontFace);
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisample state
    vk::PipelineMultisampleStateCreateInfo multisampling;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = static_cast<vk::SampleCountFlagBits>(desc.multisample.sampleCount);
    multisampling.minSampleShading = 1.0f;
    multisampling.pSampleMask = &desc.multisample.sampleMask;
    multisampling.alphaToCoverageEnable = desc.multisample.alphaToCoverageEnabled ? VK_TRUE : VK_FALSE;
    multisampling.alphaToOneEnable = VK_FALSE;

    // Depth-stencil state
    vk::PipelineDepthStencilStateCreateInfo depthStencil;
    if (desc.depthStencil) {
        depthStencil.depthTestEnable = desc.depthStencil->depthTestEnabled ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable = desc.depthStencil->depthWriteEnabled ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp = ToVkCompareOp(desc.depthStencil->depthCompare);
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = desc.depthStencil->stencilTestEnabled ? VK_TRUE : VK_FALSE;
    }

    // Color blend state
    std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments;
    for (const auto& target : desc.colorTargets) {
        vk::PipelineColorBlendAttachmentState colorBlendAttachment;
        colorBlendAttachment.colorWriteMask = ToVkColorComponentFlags(target.blend.writeMask);
        colorBlendAttachment.blendEnable = target.blend.blendEnabled ? VK_TRUE : VK_FALSE;

        if (target.blend.blendEnabled) {
            colorBlendAttachment.srcColorBlendFactor = ToVkBlendFactor(target.blend.srcColorFactor);
            colorBlendAttachment.dstColorBlendFactor = ToVkBlendFactor(target.blend.dstColorFactor);
            colorBlendAttachment.colorBlendOp = ToVkBlendOp(target.blend.colorBlendOp);
            colorBlendAttachment.srcAlphaBlendFactor = ToVkBlendFactor(target.blend.srcAlphaFactor);
            colorBlendAttachment.dstAlphaBlendFactor = ToVkBlendFactor(target.blend.dstAlphaFactor);
            colorBlendAttachment.alphaBlendOp = ToVkBlendOp(target.blend.alphaBlendOp);
        }

        colorBlendAttachments.push_back(colorBlendAttachment);
    }

    vk::PipelineColorBlendStateCreateInfo colorBlending;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = vk::LogicOp::eCopy;
    colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
    colorBlending.pAttachments = colorBlendAttachments.data();

    // Dynamic state
    std::vector<vk::DynamicState> dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };

    vk::PipelineDynamicStateCreateInfo dynamicState;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Platform-specific rendering setup
#ifdef __linux__
    // Linux: Use traditional render pass (lavapipe/Vulkan 1.1 doesn't support dynamic rendering)
    vk::RenderPass renderPass = VK_NULL_HANDLE;
    if (desc.nativeRenderPass) {
        // Convert void* back to VkRenderPass
        renderPass = reinterpret_cast<VkRenderPass>(desc.nativeRenderPass);
    }

    // Pipeline create info
    vk::GraphicsPipelineCreateInfo pipelineInfo;
    pipelineInfo.pNext = nullptr;  // No dynamic rendering on Linux
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = desc.depthStencil ? &depthStencil : nullptr;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = vulkanLayout->getVkPipelineLayout();
    pipelineInfo.renderPass = renderPass;  // Traditional render pass
    pipelineInfo.subpass = 0;
#else
    // macOS/Windows: Use dynamic rendering (Vulkan 1.3)
    std::vector<vk::Format> colorFormats;
    for (const auto& target : desc.colorTargets) {
        colorFormats.push_back(ToVkFormat(target.format));
    }

    // Initialize rendering info with explicit values
    vk::PipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorFormats.size());
    renderingInfo.pColorAttachmentFormats = colorFormats.data();
    renderingInfo.depthAttachmentFormat = vk::Format::eUndefined;
    renderingInfo.stencilAttachmentFormat = vk::Format::eUndefined;

    if (desc.depthStencil) {
        renderingInfo.depthAttachmentFormat = ToVkFormat(desc.depthStencil->format);
    }

    // Pipeline create info
    vk::GraphicsPipelineCreateInfo pipelineInfo;
    pipelineInfo.pNext = &renderingInfo;  // Enable dynamic rendering
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = desc.depthStencil ? &depthStencil : nullptr;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = vulkanLayout->getVkPipelineLayout();
    pipelineInfo.renderPass = nullptr;  // Using dynamic rendering
    pipelineInfo.subpass = 0;
#endif

    m_pipeline = vk::raii::Pipeline(m_device->getVkDevice(), nullptr, pipelineInfo);
}

VulkanRHIRenderPipeline::~VulkanRHIRenderPipeline() {
    // RAII handles cleanup automatically
}

VulkanRHIRenderPipeline::VulkanRHIRenderPipeline(VulkanRHIRenderPipeline&& other) noexcept
    : m_device(other.m_device)
    , m_pipeline(std::move(other.m_pipeline))
    , m_layout(other.m_layout)
{
}

VulkanRHIRenderPipeline& VulkanRHIRenderPipeline::operator=(VulkanRHIRenderPipeline&& other) noexcept {
    if (this != &other) {
        m_device = other.m_device;
        m_pipeline = std::move(other.m_pipeline);
        m_layout = other.m_layout;
    }
    return *this;
}

// ============================================================================
// VulkanRHIComputePipeline Implementation
// ============================================================================

VulkanRHIComputePipeline::VulkanRHIComputePipeline(VulkanRHIDevice* device, const ComputePipelineDesc& desc)
    : m_device(device)
    , m_pipeline(nullptr)
{
    if (!desc.computeShader) {
        throw std::runtime_error("Compute shader is required");
    }
    if (!desc.layout) {
        throw std::runtime_error("Pipeline layout is required");
    }

    auto* vulkanComputeShader = static_cast<VulkanRHIShader*>(desc.computeShader);
    auto* vulkanLayout = static_cast<VulkanRHIPipelineLayout*>(desc.layout);

    // Shader stage
    vk::PipelineShaderStageCreateInfo shaderStage;
    shaderStage.stage = vk::ShaderStageFlagBits::eCompute;
    shaderStage.module = vulkanComputeShader->getVkShaderModule();
    shaderStage.pName = vulkanComputeShader->getEntryPoint().c_str();

    // Pipeline create info
    vk::ComputePipelineCreateInfo pipelineInfo;
    pipelineInfo.stage = shaderStage;
    pipelineInfo.layout = vulkanLayout->getVkPipelineLayout();

    m_pipeline = vk::raii::Pipeline(m_device->getVkDevice(), nullptr, pipelineInfo);
}

VulkanRHIComputePipeline::~VulkanRHIComputePipeline() {
    // RAII handles cleanup automatically
}

VulkanRHIComputePipeline::VulkanRHIComputePipeline(VulkanRHIComputePipeline&& other) noexcept
    : m_device(other.m_device)
    , m_pipeline(std::move(other.m_pipeline))
{
}

VulkanRHIComputePipeline& VulkanRHIComputePipeline::operator=(VulkanRHIComputePipeline&& other) noexcept {
    if (this != &other) {
        m_device = other.m_device;
        m_pipeline = std::move(other.m_pipeline);
    }
    return *this;
}

} // namespace Vulkan
} // namespace RHI
