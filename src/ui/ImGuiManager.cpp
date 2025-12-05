#include "ImGuiManager.hpp"
#include "src/core/PlatformConfig.hpp"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <stdexcept>

ImGuiManager::ImGuiManager(GLFWwindow* window,
                           VulkanDevice& device,
                           VulkanSwapchain& swapchain,
                           CommandManager& commandManager)
    : device(device), swapchain(swapchain), commandManager(commandManager) {

    createDescriptorPool();
    initImGui(window);
}

ImGuiManager::~ImGuiManager() {
    cleanup();
}

void ImGuiManager::createDescriptorPool() {
    // Create descriptor pool for ImGui
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

    imguiPool = vk::raii::DescriptorPool(device.getDevice(), poolInfo);
}

void ImGuiManager::initImGui(GLFWwindow* window) {
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForVulkan(window, true);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = static_cast<VkInstance>(*device.getInstance());
    initInfo.PhysicalDevice = static_cast<VkPhysicalDevice>(*device.getPhysicalDevice());
    initInfo.Device = static_cast<VkDevice>(*device.getDevice());
    initInfo.QueueFamily = device.getGraphicsQueueFamily();
    initInfo.Queue = static_cast<VkQueue>(*device.getGraphicsQueue());
    initInfo.DescriptorPool = static_cast<VkDescriptorPool>(*imguiPool);
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = swapchain.getImageCount();
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

#ifdef __linux__
    // Linux: Use render pass
    initInfo.RenderPass = static_cast<VkRenderPass>(*swapchain.getRenderPass());
    initInfo.UseDynamicRendering = false;
#else
    // macOS/Windows: Use dynamic rendering (Vulkan 1.3)
    initInfo.RenderPass = VK_NULL_HANDLE;
    initInfo.UseDynamicRendering = true;

    // Specify color attachment format for dynamic rendering
    VkFormat colorFormat = static_cast<VkFormat>(swapchain.getFormat());
    initInfo.PipelineRenderingCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorFormat
    };
#endif

    // Initialize ImGui Vulkan
    ImGui_ImplVulkan_Init(&initInfo);

    // Upload Fonts
    auto commandBuffer = commandManager.beginSingleTimeCommands();
    ImGui_ImplVulkan_CreateFontsTexture();
    commandManager.endSingleTimeCommands(*commandBuffer);
    ImGui_ImplVulkan_DestroyFontsTexture();
}

void ImGuiManager::cleanup() {
    if (ImGui::GetCurrentContext()) {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
}

void ImGuiManager::newFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiManager::renderUI(Camera& camera, bool isFdfMode,
                            std::function<void()> onModeToggle,
                            std::function<void(const std::string&)> onFileLoad) {
    // Main control window
    ImGui::Begin("FdF Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::Text("Vulkan FdF Wireframe Visualizer");
    ImGui::Separator();

    // Mode control
    ImGui::Text("Rendering Mode:");
    if (ImGui::Button(isFdfMode ? "Mode: FDF (Wireframe)" : "Mode: OBJ (Solid)")) {
        if (onModeToggle) onModeToggle();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Toggle between FDF wireframe and OBJ solid rendering");
    }

    ImGui::Separator();

    // Camera controls
    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Projection mode
        ProjectionMode currentMode = camera.getProjectionMode();
        const char* projectionModes[] = { "Perspective", "Isometric" };
        int currentProjection = (currentMode == ProjectionMode::Perspective) ? 0 : 1;

        if (ImGui::Combo("Projection", &currentProjection, projectionModes, 2)) {
            camera.setProjectionMode(
                currentProjection == 0 ? ProjectionMode::Perspective : ProjectionMode::Isometric
            );
        }

        // Reset camera
        if (ImGui::Button("Reset Camera")) {
            camera.reset();
        }

        ImGui::Text("Speed Controls:");
        ImGui::SliderFloat("Move Speed", &moveSpeed, 0.1f, 5.0f);
        ImGui::SliderFloat("Rotate Speed", &rotateSpeed, 0.1f, 2.0f);
        ImGui::SliderFloat("Zoom Speed", &zoomSpeed, 0.1f, 3.0f);
    }

    ImGui::Separator();

    // File loading
    if (ImGui::CollapsingHeader("File Loading")) {
        ImGui::InputText("File Path", filePathBuffer, sizeof(filePathBuffer));
        if (ImGui::Button("Load File")) {
            if (onFileLoad) onFileLoad(std::string(filePathBuffer));
        }

        ImGui::Text("Quick Load:");
        if (ImGui::Button("test.fdf")) {
            if (onFileLoad) onFileLoad("models/test.fdf");
        }
        ImGui::SameLine();
        if (ImGui::Button("pyramid.fdf")) {
            if (onFileLoad) onFileLoad("models/pyramid.fdf");
        }
    }

    ImGui::Separator();

    // Controls help
    if (ImGui::CollapsingHeader("Controls Help")) {
        ImGui::BulletText("Left Mouse + Drag: Rotate camera");
        ImGui::BulletText("Mouse Wheel: Zoom in/out");
        ImGui::BulletText("W/A/S/D: Move camera");
        ImGui::BulletText("P or I: Toggle projection");
        ImGui::BulletText("R: Reset camera");
        ImGui::BulletText("ESC: Exit");
    }

    ImGui::Separator();

    // Statistics
    if (ImGui::CollapsingHeader("Statistics")) {
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Text("Frame Time: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);
    }

    // Demo window toggle
    ImGui::Separator();
    ImGui::Checkbox("Show ImGui Demo", &showDemoWindow);

    ImGui::End();

    // Show demo window if enabled
    if (showDemoWindow) {
        ImGui::ShowDemoWindow(&showDemoWindow);
    }
}

void ImGuiManager::render(const vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex) {
    ImGui::Render();

#ifdef __linux__
    // Linux: ImGui renders within the main render pass
    VkCommandBuffer vkCmdBuffer = static_cast<VkCommandBuffer>(*commandBuffer);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vkCmdBuffer);
#else
    // macOS/Windows: Use dynamic rendering for ImGui
    vk::RenderingAttachmentInfo colorAttachment{
        .imageView = swapchain.getImageViews()[imageIndex],
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eLoad,  // Load existing content
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f)
    };

    vk::RenderingInfo renderingInfo{
        .renderArea = {
            .offset = {0, 0},
            .extent = swapchain.getExtent()
        },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment
    };

    commandBuffer.beginRendering(renderingInfo);
    VkCommandBuffer vkCmdBuffer = static_cast<VkCommandBuffer>(*commandBuffer);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vkCmdBuffer);
    commandBuffer.endRendering();
#endif
}

void ImGuiManager::handleResize() {
    // ImGui handles resize automatically
}
