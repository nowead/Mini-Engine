#include "ImGuiVulkanBackend.hpp"
#include <rhi/vulkan/VulkanRHICommandEncoder.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <stdexcept>
#include <array>

namespace ui {

ImGuiVulkanBackend::~ImGuiVulkanBackend() {
    shutdown();
}

void ImGuiVulkanBackend::init(GLFWwindow* window,
                              rhi::RHIDevice* device,
                              rhi::RHISwapchain* swapchain) {
    // Cast RHI to Vulkan-specific types
    vulkanDevice = static_cast<RHI::Vulkan::VulkanRHIDevice*>(device);
    vulkanSwapchain = static_cast<RHI::Vulkan::VulkanRHISwapchain*>(swapchain);

    if (!vulkanDevice || !vulkanSwapchain) {
        throw std::runtime_error("ImGuiVulkanBackend requires Vulkan RHI backend");
    }

    // Create descriptor pool for ImGui
    createDescriptorPool();

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform backend (GLFW)
    ImGui_ImplGlfw_InitForVulkan(window, true);

    // Setup Renderer backend (Vulkan)
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = static_cast<VkInstance>(*vulkanDevice->getVkInstance());
    initInfo.PhysicalDevice = static_cast<VkPhysicalDevice>(*vulkanDevice->getVkPhysicalDevice());
    initInfo.Device = static_cast<VkDevice>(*vulkanDevice->getVkDevice());
    initInfo.QueueFamily = vulkanDevice->getGraphicsQueueFamilyIndex();
    initInfo.Queue = static_cast<VkQueue>(*vulkanDevice->getVkGraphicsQueue());
    initInfo.DescriptorPool = static_cast<VkDescriptorPool>(*descriptorPool);
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = vulkanSwapchain->getBufferCount();
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

#ifdef __linux__
    // Linux: Use render pass (Vulkan 1.1 compatibility)
    // Create render pass if not already created
    if (vulkanSwapchain->getRenderPass() == VK_NULL_HANDLE) {
        vulkanSwapchain->createRenderPass();
    }
    initInfo.RenderPass = static_cast<VkRenderPass>(vulkanSwapchain->getRenderPass());
    initInfo.UseDynamicRendering = false;
#else
    // macOS/Windows: Use dynamic rendering (Vulkan 1.3)
    initInfo.RenderPass = VK_NULL_HANDLE;
    initInfo.UseDynamicRendering = true;

    // Specify color attachment format for dynamic rendering
    VkFormat colorFormat = static_cast<VkFormat>(vulkanSwapchain->getFormat());
    initInfo.PipelineRenderingCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorFormat
    };
#endif

    // Initialize ImGui Vulkan renderer
    ImGui_ImplVulkan_Init(&initInfo);

    // Upload font textures (using direct RHI, no CommandManager)
    uploadFonts();
}

void ImGuiVulkanBackend::newFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiVulkanBackend::render(rhi::RHICommandEncoder* encoder,
                                uint32_t imageIndex) {
    // Finalize ImGui draw data
    ImGui::Render();

    // Cast encoder to Vulkan command buffer
    auto* vulkanEncoder = static_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(encoder);
    auto& commandBuffer = vulkanEncoder->getCommandBuffer();

    // Render ImGui draw data
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(),
                                    static_cast<VkCommandBuffer>(*commandBuffer));
}

void ImGuiVulkanBackend::handleResize() {
    // ImGui Vulkan backend doesn't require special resize handling
    // The swapchain is recreated externally, and ImGui adapts automatically
}

void ImGuiVulkanBackend::shutdown() {
    if (ImGui::GetCurrentContext()) {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    // Descriptor pool will be automatically destroyed by RAII
}

void ImGuiVulkanBackend::createDescriptorPool() {
    // Create descriptor pool for ImGui with generous limits
    std::array<vk::DescriptorPoolSize, 11> poolSizes = {{
        { vk::DescriptorType::eSampler, 1000 },
        { vk::DescriptorType::eCombinedImageSampler, 1000 },
        { vk::DescriptorType::eSampledImage, 1000 },
        { vk::DescriptorType::eStorageImage, 1000 },
        { vk::DescriptorType::eUniformTexelBuffer, 1000 },
        { vk::DescriptorType::eStorageTexelBuffer, 1000 },
        { vk::DescriptorType::eUniformBuffer, 1000 },
        { vk::DescriptorType::eStorageBuffer, 1000 },
        { vk::DescriptorType::eUniformBufferDynamic, 1000 },
        { vk::DescriptorType::eStorageBufferDynamic, 1000 },
        { vk::DescriptorType::eInputAttachment, 1000 }
    }};

    vk::DescriptorPoolCreateInfo poolInfo;
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    descriptorPool = vk::raii::DescriptorPool(vulkanDevice->getVkDevice(), poolInfo);
}

void ImGuiVulkanBackend::uploadFonts() {
    // Direct RHI usage for font upload (replaces CommandManager)
    // This follows the same pattern as Phase 5 (Mesh/ResourceManager)

    auto encoder = vulkanDevice->createCommandEncoder();

    // Create font textures - this internally records commands to the current command buffer
    // We need to extract the native Vulkan command buffer for imgui_impl_vulkan
    auto* vulkanEncoder = static_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(encoder.get());
    auto& commandBuffer = vulkanEncoder->getCommandBuffer();

    ImGui_ImplVulkan_CreateFontsTexture();

    // Finish recording and submit
    auto cmdBuffer = encoder->finish();
    auto* queue = vulkanDevice->getQueue(rhi::QueueType::Graphics);
    queue->submit(cmdBuffer.get());
    queue->waitIdle();

    // Destroy staging resources
    ImGui_ImplVulkan_DestroyFontsTexture();
}

} // namespace ui
