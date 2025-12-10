#include "VulkanPipeline.hpp"
#include "../core/PlatformConfig.hpp"
#include "../utils/Vertex.hpp"
#include "../utils/FileUtils.hpp"

VulkanPipeline::VulkanPipeline(
    VulkanDevice& device,
    const VulkanSwapchain& swapchain,
    const std::string& shaderPath,
    vk::Format depthFormat,
    vk::RenderPass renderPass,
    TopologyMode topology)
    : device(device) {

    createDescriptorSetLayout();
    createPipelineLayout();
    createGraphicsPipeline(shaderPath, swapchain.getFormat(), depthFormat, renderPass, topology);
}

void VulkanPipeline::createDescriptorSetLayout() {
    std::array bindings = {
        vk::DescriptorSetLayoutBinding(
            0, 
            vk::DescriptorType::eUniformBuffer, 
            1, 
            vk::ShaderStageFlagBits::eVertex, 
            nullptr),
        vk::DescriptorSetLayoutBinding(
            1, 
            vk::DescriptorType::eCombinedImageSampler, 
            1, 
            vk::ShaderStageFlagBits::eFragment, 
            nullptr)
    };

    vk::DescriptorSetLayoutCreateInfo layoutInfo{
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings = bindings.data()
    };

    descriptorSetLayout = vk::raii::DescriptorSetLayout(device.getDevice(), layoutInfo);
}

void VulkanPipeline::createPipelineLayout() {
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
        .setLayoutCount = 1,
        .pSetLayouts = &*descriptorSetLayout,
        .pushConstantRangeCount = 0
    };

    pipelineLayout = vk::raii::PipelineLayout(device.getDevice(), pipelineLayoutInfo);
}

void VulkanPipeline::createGraphicsPipeline(
    const std::string& shaderPath,
    vk::Format colorFormat,
    vk::Format depthFormat,
    vk::RenderPass renderPass,
    TopologyMode topology) {

    vk::raii::ShaderModule shaderModule = createShaderModule(FileUtils::readFile(shaderPath));

    vk::PipelineShaderStageCreateInfo vertShaderStageInfo{
        .stage = vk::ShaderStageFlagBits::eVertex,
        .module = shaderModule,
        .pName = "vertMain"
    };

    vk::PipelineShaderStageCreateInfo fragShaderStageInfo{
        .stage = vk::ShaderStageFlagBits::eFragment,
        .module = shaderModule,
        .pName = "fragMain"
    };

    vk::PipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // Vertex input
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &bindingDescription,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
        .pVertexAttributeDescriptions = attributeDescriptions.data()
    };

    // Input assembly - select topology based on mode
    vk::PrimitiveTopology vkTopology = topology == TopologyMode::LineList
        ? vk::PrimitiveTopology::eLineList
        : vk::PrimitiveTopology::eTriangleList;

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
        .topology = vkTopology,
        .primitiveRestartEnable = vk::False
    };

    // Viewport state (dynamic)
    vk::PipelineViewportStateCreateInfo viewportState{
        .viewportCount = 1,
        .scissorCount = 1
    };

    // Rasterization - disable culling for wireframe mode
    vk::CullModeFlags cullMode = topology == TopologyMode::LineList
        ? vk::CullModeFlagBits::eNone
        : vk::CullModeFlagBits::eBack;

    vk::PipelineRasterizationStateCreateInfo rasterizer{
        .depthClampEnable = vk::False,
        .rasterizerDiscardEnable = vk::False,
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = cullMode,
        .frontFace = vk::FrontFace::eCounterClockwise,
        .depthBiasEnable = vk::False,
        .lineWidth = 1.0f
    };

    // Multisampling
    vk::PipelineMultisampleStateCreateInfo multisampling{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable = vk::False
    };

    // Depth stencil
    // For wireframe: disable depth write to see all lines through the mesh
    vk::Bool32 depthWrite = topology == TopologyMode::LineList ? vk::False : vk::True;
    vk::PipelineDepthStencilStateCreateInfo depthStencil{
        .depthTestEnable = vk::True,
        .depthWriteEnable = depthWrite,
        .depthCompareOp = vk::CompareOp::eLess,
        .depthBoundsTestEnable = vk::False,
        .stencilTestEnable = vk::False
    };

    // Color blending
    vk::PipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable = vk::False,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | 
                         vk::ColorComponentFlagBits::eG | 
                         vk::ColorComponentFlagBits::eB | 
                         vk::ColorComponentFlagBits::eA
    };

    vk::PipelineColorBlendStateCreateInfo colorBlending{
        .logicOpEnable = vk::False,
        .logicOp = vk::LogicOp::eCopy,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment
    };

    // Dynamic states
    std::vector dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };

    vk::PipelineDynamicStateCreateInfo dynamicState{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()
    };

    // Platform-specific pipeline creation
    if constexpr (!Platform::USE_DYNAMIC_RENDERING) {
        // Linux: Use traditional render pass (Vulkan 1.1)
        vk::GraphicsPipelineCreateInfo pipelineInfo{
            .stageCount = 2,
            .pStages = shaderStages,
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &inputAssembly,
            .pViewportState = &viewportState,
            .pRasterizationState = &rasterizer,
            .pMultisampleState = &multisampling,
            .pDepthStencilState = &depthStencil,
            .pColorBlendState = &colorBlending,
            .pDynamicState = &dynamicState,
            .layout = *pipelineLayout,
            .renderPass = renderPass,
            .subpass = 0
        };

        graphicsPipeline = vk::raii::Pipeline(
            device.getDevice(),
            nullptr,
            pipelineInfo
        );
    } else {
        // macOS/Windows: Use dynamic rendering (Vulkan 1.3)
        vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipelineCreateInfoChain = {
            {
                .stageCount = 2,
                .pStages = shaderStages,
                .pVertexInputState = &vertexInputInfo,
                .pInputAssemblyState = &inputAssembly,
                .pViewportState = &viewportState,
                .pRasterizationState = &rasterizer,
                .pMultisampleState = &multisampling,
                .pDepthStencilState = &depthStencil,
                .pColorBlendState = &colorBlending,
                .pDynamicState = &dynamicState,
                .layout = *pipelineLayout,
                .renderPass = nullptr
            },
            {
                .colorAttachmentCount = 1,
                .pColorAttachmentFormats = &colorFormat,
                .depthAttachmentFormat = depthFormat
            }
        };

        graphicsPipeline = vk::raii::Pipeline(
            device.getDevice(),
            nullptr,
            pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>()
        );
    }
}

vk::raii::ShaderModule VulkanPipeline::createShaderModule(const std::vector<char>& code) {
    vk::ShaderModuleCreateInfo createInfo{
        .codeSize = code.size(),
        .pCode = reinterpret_cast<const uint32_t*>(code.data())
    };

    return vk::raii::ShaderModule(device.getDevice(), createInfo);
}

void VulkanPipeline::bind(const vk::raii::CommandBuffer& commandBuffer) const {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *graphicsPipeline);
}
