#pragma once

#include "../rhi/include/rhi/RHIDevice.hpp"
#include "../rhi/include/rhi/RHIBuffer.hpp"
#include "../rhi/include/rhi/RHICommandBuffer.hpp"
#include "../rhi/include/rhi/RHIShader.hpp"
#include "../rhi/include/rhi/RHIBindGroup.hpp"
#include "../rhi/include/rhi/RHIPipeline.hpp"
#include <memory>

namespace examples {

/**
 * @brief GPU Instancing test demonstration
 *
 * Renders 1000 cubes using a single draw call to demonstrate GPU instancing performance.
 * This is a critical feature for the stock/crypto visualization platform which needs to
 * render thousands of buildings (stocks) simultaneously.
 *
 * Performance target: 1000 instances @ 60 FPS
 */
class InstancingTest {
public:
    InstancingTest(rhi::RHIDevice* device, int width, int height, void* nativeRenderPass = nullptr);
    ~InstancingTest();

    void init();
    void update(float deltaTime);
    void render(rhi::RHIRenderPassEncoder* encoder);
    void resize(int width, int height);

    // Camera controls
    void onMouseMove(double xpos, double ypos);
    void onMouseButton(int button, int action);
    void onKeyPress(int key, int action);

private:
    void createCubeGeometry();
    void createInstanceData();
    void createUniformBuffer();
    void createPipeline();

    static constexpr int INSTANCE_COUNT = 1000;

    rhi::RHIDevice* m_device;
    void* m_nativeRenderPass;  // Platform-specific render pass (VkRenderPass on Linux)
    int m_width;
    int m_height;
    float m_time = 0.0f;

    // Camera state
    float m_cameraDistance = 50.0f;
    float m_cameraYaw = 0.0f;
    float m_cameraPitch = 20.0f;
    bool m_mousePressed = false;
    double m_lastMouseX = 0.0;
    double m_lastMouseY = 0.0;
    bool m_autoRotate = true;

    // GPU resources
    std::unique_ptr<rhi::RHIBuffer> m_vertexBuffer;
    std::unique_ptr<rhi::RHIBuffer> m_indexBuffer;
    std::unique_ptr<rhi::RHIBuffer> m_instanceBuffer;  // NEW: Per-instance data
    std::unique_ptr<rhi::RHIBuffer> m_uniformBuffer;

    // Pipeline resources
    std::unique_ptr<rhi::RHIShader> m_vertexShader;
    std::unique_ptr<rhi::RHIShader> m_fragmentShader;
    std::unique_ptr<rhi::RHIBindGroupLayout> m_bindGroupLayout;
    std::unique_ptr<rhi::RHIBindGroup> m_bindGroup;
    std::unique_ptr<rhi::RHIPipelineLayout> m_pipelineLayout;
    std::unique_ptr<rhi::RHIRenderPipeline> m_pipeline;

    uint32_t m_indexCount = 0;
};

} // namespace examples
