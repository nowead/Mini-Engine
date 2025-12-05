#pragma once

#include "../utils/VulkanCommon.hpp"
#include "../core/VulkanDevice.hpp"
#include "VulkanSwapchain.hpp"
#include <string>

/**
 * @brief Primitive topology mode for pipeline
 */
enum class TopologyMode {
    TriangleList,
    LineList
};

/**
 * @brief Manages Vulkan graphics pipeline and associated layouts
 *
 * Handles descriptor set layout, pipeline layout, and graphics pipeline creation
 * with support for dynamic rendering.
 */
class VulkanPipeline {
public:
    /**
     * @brief Construct graphics pipeline with shader and configuration
     * @param device Vulkan device reference
     * @param swapchain Swapchain for format information
     * @param shaderPath Path to compiled shader SPIR-V file
     * @param depthFormat Depth buffer format
     * @param renderPass Render pass (Linux only, ignored on other platforms)
     * @param topology Primitive topology (default: TriangleList)
     */
    VulkanPipeline(
        VulkanDevice& device,
        const VulkanSwapchain& swapchain,
        const std::string& shaderPath,
        vk::Format depthFormat,
        vk::RenderPass renderPass = nullptr,
        TopologyMode topology = TopologyMode::TriangleList);

    ~VulkanPipeline() = default;

    // Disable copy, enable move construction only
    VulkanPipeline(const VulkanPipeline&) = delete;
    VulkanPipeline& operator=(const VulkanPipeline&) = delete;
    VulkanPipeline(VulkanPipeline&&) = default;
    VulkanPipeline& operator=(VulkanPipeline&&) = delete;

    // Accessors
    vk::Pipeline getPipeline() const { return *graphicsPipeline; }
    vk::PipelineLayout getPipelineLayout() const { return *pipelineLayout; }
    vk::DescriptorSetLayout getDescriptorSetLayout() const { return *descriptorSetLayout; }

    // Bind pipeline to command buffer
    void bind(const vk::raii::CommandBuffer& commandBuffer) const;

private:
    VulkanDevice& device;

    vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::Pipeline graphicsPipeline = nullptr;

    void createDescriptorSetLayout();
    void createPipelineLayout();
    void createGraphicsPipeline(
        const std::string& shaderPath,
        vk::Format colorFormat,
        vk::Format depthFormat,
        vk::RenderPass renderPass,
        TopologyMode topology);

    vk::raii::ShaderModule createShaderModule(const std::vector<char>& code);
};
