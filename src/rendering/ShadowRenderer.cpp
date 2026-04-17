#include "ShadowRenderer.hpp"
#include "src/utils/FileUtils.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cstring>
#include <iostream>

#ifdef __linux__
#include <rhi/vulkan/VulkanRHIDevice.hpp>
#include <rhi/vulkan/VulkanRHITexture.hpp>
#endif

namespace rendering {

ShadowRenderer::ShadowRenderer(rhi::RHIDevice* device, rhi::RHIQueue* queue)
    : m_device(device), m_queue(queue) {}

ShadowRenderer::~ShadowRenderer() {
#ifdef __linux__
    if (m_device) {
        auto* vulkanDevice = dynamic_cast<RHI::Vulkan::VulkanRHIDevice*>(m_device);
        if (vulkanDevice) {
            VkDevice vkDevice = static_cast<VkDevice>(*vulkanDevice->getVkDevice());
            for (auto& fb : m_nativeFramebuffers) {
                if (fb != VK_NULL_HANDLE) {
                    vkDestroyFramebuffer(vkDevice, fb, nullptr);
                    fb = VK_NULL_HANDLE;
                }
            }
            if (m_nativeRenderPass != VK_NULL_HANDLE) {
                vkDestroyRenderPass(vkDevice, m_nativeRenderPass, nullptr);
                m_nativeRenderPass = VK_NULL_HANDLE;
            }
        }
    }
#endif
}

bool ShadowRenderer::initialize(void* nativeRenderPass, rhi::RHIBindGroupLayout* ssboLayout) {
    if (!m_device || !m_queue) {
        std::cerr << "[ShadowRenderer] Invalid device or queue\n";
        return false;
    }
    if (!createShadowMap())      { std::cerr << "[ShadowRenderer] createShadowMap failed\n";      return false; }
    if (!createShadowSampler())  { std::cerr << "[ShadowRenderer] createShadowSampler failed\n";  return false; }
    if (!createShaders())        { std::cerr << "[ShadowRenderer] createShaders failed\n";        return false; }
    if (!createUniformBuffers()) { std::cerr << "[ShadowRenderer] createUniformBuffers failed\n"; return false; }
    if (!createBindGroups())     { std::cerr << "[ShadowRenderer] createBindGroups failed\n";     return false; }

#ifdef __linux__
    if (!createLinuxRenderPass())    { std::cerr << "[ShadowRenderer] createLinuxRenderPass failed\n";    return false; }
    if (!createLinuxFramebuffers())  { std::cerr << "[ShadowRenderer] createLinuxFramebuffers failed\n";  return false; }
    nativeRenderPass = m_nativeRenderPass;
#endif

    if (!createPipeline(nativeRenderPass, ssboLayout)) {
        std::cerr << "[ShadowRenderer] createPipeline failed\n";
        return false;
    }

    m_initialized = true;
    std::cout << "[ShadowRenderer] CSM initialized (" << SHADOW_MAP_SIZE << "x"
              << SHADOW_MAP_SIZE << " × " << NUM_CASCADES << " cascades)\n";
    return true;
}

// ---------------------------------------------------------------------------
// Resource creation
// ---------------------------------------------------------------------------

bool ShadowRenderer::createShadowMap() {
    rhi::TextureDesc desc;
    desc.size            = rhi::Extent3D(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 1);
    desc.format          = rhi::TextureFormat::Depth32Float;
    desc.usage           = rhi::TextureUsage::DepthStencil | rhi::TextureUsage::Sampled;
    desc.arrayLayerCount = NUM_CASCADES;
    desc.label           = "ShadowCSMArray";

    m_shadowMap = m_device->createTexture(desc);
    if (!m_shadowMap) { std::cerr << "[ShadowRenderer] Failed to create CSM texture\n"; return false; }

    // Full array view for sampling in fragment shader
    rhi::TextureViewDesc arrayViewDesc;
    arrayViewDesc.format          = rhi::TextureFormat::Depth32Float;
    arrayViewDesc.dimension       = rhi::TextureViewDimension::View2DArray;
    arrayViewDesc.baseArrayLayer  = 0;
    arrayViewDesc.arrayLayerCount = NUM_CASCADES;
    arrayViewDesc.label           = "ShadowCSMArrayView";

    m_shadowMapView = m_shadowMap->createView(arrayViewDesc);
    if (!m_shadowMapView) { std::cerr << "[ShadowRenderer] Failed to create CSM array view\n"; return false; }

    // Per-cascade 2D views for rendering
    for (uint32_t i = 0; i < NUM_CASCADES; ++i) {
        rhi::TextureViewDesc layerDesc;
        layerDesc.format          = rhi::TextureFormat::Depth32Float;
        layerDesc.dimension       = rhi::TextureViewDimension::View2D;
        layerDesc.baseArrayLayer  = i;
        layerDesc.arrayLayerCount = 1;
        layerDesc.label           = "ShadowCascadeView";

        m_cascadeViews[i] = m_shadowMap->createView(layerDesc);
        if (!m_cascadeViews[i]) {
            std::cerr << "[ShadowRenderer] Failed to create cascade view " << i << "\n";
            return false;
        }
    }

    std::cout << "[ShadowRenderer] CSM texture created\n";
    return true;
}

bool ShadowRenderer::createShadowSampler() {
    rhi::SamplerDesc desc;
    desc.magFilter      = rhi::FilterMode::Nearest;
    desc.minFilter      = rhi::FilterMode::Nearest;
    desc.mipmapFilter   = rhi::MipmapMode::Nearest;
    desc.addressModeU   = rhi::AddressMode::ClampToEdge;
    desc.addressModeV   = rhi::AddressMode::ClampToEdge;
    desc.addressModeW   = rhi::AddressMode::ClampToEdge;
    desc.compareEnable  = false;
    desc.label          = "ShadowSampler";

    m_shadowSampler = m_device->createSampler(desc);
    return m_shadowSampler != nullptr;
}

bool ShadowRenderer::createShaders() {
#ifdef __EMSCRIPTEN__
    auto wgslCodeRaw = FileUtils::readFile("shaders/shadow.wgsl");
    if (wgslCodeRaw.empty()) { std::cerr << "[ShadowRenderer] Failed to load shadow.wgsl\n"; return false; }
    std::vector<uint8_t> wgslCode(wgslCodeRaw.begin(), wgslCodeRaw.end());

    rhi::ShaderSource vertSrc(rhi::ShaderLanguage::WGSL, wgslCode, rhi::ShaderStage::Vertex,   "vs_main");
    rhi::ShaderSource fragSrc(rhi::ShaderLanguage::WGSL, wgslCode, rhi::ShaderStage::Fragment, "fs_main");
    m_vertexShader   = m_device->createShader(rhi::ShaderDesc(vertSrc, "ShadowVS"));
    m_fragmentShader = m_device->createShader(rhi::ShaderDesc(fragSrc, "ShadowFS"));
#else
    auto loadSpv = [&](const char* path, rhi::ShaderStage stage, const char* label) {
        auto raw = FileUtils::readFile(path);
        if (raw.empty()) { std::cerr << "[ShadowRenderer] Failed to load " << path << "\n"; return std::unique_ptr<rhi::RHIShader>{}; }
        std::vector<uint8_t> code(raw.begin(), raw.end());
        rhi::ShaderSource src(rhi::ShaderLanguage::SPIRV, code, stage, "main");
        return m_device->createShader(rhi::ShaderDesc(src, label));
    };
    m_vertexShader   = loadSpv("shaders/shadow.vert.spv", rhi::ShaderStage::Vertex,   "ShadowVS");
    m_fragmentShader = loadSpv("shaders/shadow.frag.spv", rhi::ShaderStage::Fragment, "ShadowFS");
#endif
    return m_vertexShader && m_fragmentShader;
}

bool ShadowRenderer::createUniformBuffers() {
    for (size_t f = 0; f < MAX_FRAMES_IN_FLIGHT; ++f) {
        for (size_t c = 0; c < NUM_CASCADES; ++c) {
            rhi::BufferDesc desc;
            desc.size             = sizeof(LightSpaceUBO);
            desc.usage            = rhi::BufferUsage::Uniform | rhi::BufferUsage::MapWrite;
            desc.mappedAtCreation = false;
            desc.label            = "ShadowCascadeUBO";
            m_uniformBuffers[f][c] = m_device->createBuffer(desc);
            if (!m_uniformBuffers[f][c]) {
                std::cerr << "[ShadowRenderer] Failed to create UBO [" << f << "][" << c << "]\n";
                return false;
            }
        }
    }
    return true;
}

bool ShadowRenderer::createBindGroups() {
    rhi::BindGroupLayoutDesc layoutDesc;
    layoutDesc.entries.push_back(rhi::BindGroupLayoutEntry(0, rhi::ShaderStage::Vertex, rhi::BindingType::UniformBuffer));
    layoutDesc.label = "ShadowBindGroupLayout";

    m_bindGroupLayout = m_device->createBindGroupLayout(layoutDesc);
    if (!m_bindGroupLayout) {
        std::cerr << "[ShadowRenderer] Failed to create bind group layout\n";
        return false;
    }

    for (size_t f = 0; f < MAX_FRAMES_IN_FLIGHT; ++f) {
        for (size_t c = 0; c < NUM_CASCADES; ++c) {
            rhi::BindGroupDesc groupDesc;
            groupDesc.layout = m_bindGroupLayout.get();
            groupDesc.entries.push_back(rhi::BindGroupEntry::Buffer(0, m_uniformBuffers[f][c].get(), 0, sizeof(LightSpaceUBO)));
            groupDesc.label = "ShadowBindGroup";

            m_bindGroups[f][c] = m_device->createBindGroup(groupDesc);
            if (!m_bindGroups[f][c]) {
                std::cerr << "[ShadowRenderer] Failed to create bind group [" << f << "][" << c << "]\n";
                return false;
            }
        }
    }
    return true;
}

bool ShadowRenderer::createPipeline(void* nativeRenderPass, rhi::RHIBindGroupLayout* ssboLayout) {
    rhi::PipelineLayoutDesc layoutDesc;
    layoutDesc.bindGroupLayouts.push_back(m_bindGroupLayout.get());
    if (ssboLayout) layoutDesc.bindGroupLayouts.push_back(ssboLayout);
    layoutDesc.label = "ShadowPipelineLayout";

    m_pipelineLayout = m_device->createPipelineLayout(layoutDesc);
    if (!m_pipelineLayout) return false;

    rhi::RenderPipelineDesc pipelineDesc;
    pipelineDesc.label          = "ShadowPipeline";
    pipelineDesc.layout         = m_pipelineLayout.get();
    pipelineDesc.vertexShader   = m_vertexShader.get();
    pipelineDesc.fragmentShader = m_fragmentShader.get();

    rhi::VertexBufferLayout vertexLayout;
    vertexLayout.stride    = sizeof(float) * 8;
    vertexLayout.inputRate = rhi::VertexInputRate::Vertex;
    vertexLayout.attributes = {
        rhi::VertexAttribute(0, 0, rhi::TextureFormat::RGB32Float, 0),
        rhi::VertexAttribute(1, 0, rhi::TextureFormat::RGB32Float, sizeof(float) * 3),
        rhi::VertexAttribute(2, 0, rhi::TextureFormat::RG32Float,  sizeof(float) * 6)
    };
    pipelineDesc.vertex.buffers.push_back(vertexLayout);

    pipelineDesc.primitive.topology  = rhi::PrimitiveTopology::TriangleList;
    pipelineDesc.primitive.cullMode  = rhi::CullMode::Front;
    pipelineDesc.primitive.frontFace = rhi::FrontFace::Clockwise;

    rhi::DepthStencilState depthState;
    depthState.depthTestEnabled  = true;
    depthState.depthWriteEnabled = true;
    depthState.depthCompare      = rhi::CompareOp::Less;
    depthState.format            = rhi::TextureFormat::Depth32Float;
    pipelineDesc.depthStencil    = &depthState;

    pipelineDesc.nativeRenderPass = nativeRenderPass;

    m_pipeline = m_device->createRenderPipeline(pipelineDesc);
    return m_pipeline != nullptr;
}

// ---------------------------------------------------------------------------
// CSM matrix computation
// ---------------------------------------------------------------------------

/**
 * Compute the light-space orthographic matrix that tightly covers
 * the camera frustum sub-slice [nearSlice, farSlice].
 *
 * Algorithm:
 *   1. Extract cameraNear/cameraFar from cameraProj using Z-row coefficients
 *      (immune to Y-flip — [2][2] and [3][2] are unaffected by proj[1][1] *= -1).
 *   2. Unproject the 8 NDC corners of the FULL frustum to world space via inv(P*V).
 *   3. Linearly interpolate the near/far corner rays to get the sub-slice corners
 *      at [nearSlice, farSlice] in view depth.
 *   4. Fit a tight light-space AABB around those sub-corners, extend Z to catch
 *      shadow casters outside the camera frustum, then build the ortho matrix.
 *
 * Replaces the previous approach which extracted fovY from cameraProj[1][1],
 * which was negative after the Vulkan Y-flip and produced garbage sub-projections.
 */
glm::mat4 ShadowRenderer::computeCascadeMatrix(const glm::vec3& lightDir,
                                                const glm::mat4& cameraView,
                                                const glm::mat4& cameraProj,
                                                float nearSlice, float farSlice) {
    // ── Step 1: Extract camera near/far from projection matrix ─────────────
    // For OpenGL-convention perspective (Y-flip doesn't touch these entries):
    //   proj[2][2] = A = -(far + near) / (far - near)
    //   proj[3][2] = B = -2 * far * near / (far - near)
    //   => near = B / (A - 1),  far = B / (A + 1)
    float A = cameraProj[2][2];
    float B = cameraProj[3][2];
    float cameraNear = B / (A - 1.0f);
    float cameraFar  = B / (A + 1.0f);

    // Guard against degenerate projection (e.g. infinite far plane)
    if (std::abs(cameraFar - cameraNear) < 1.0f) {
        cameraNear = 0.1f;
        cameraFar  = 1000.0f;
    }

    // ── Step 2: Unproject full-frustum NDC corners to world space ──────────
    // Use z=0 (near, Vulkan NDC) and z=1 (far) — matches shader depth convention.
    glm::mat4 invVP = glm::inverse(cameraProj * cameraView);

    const glm::vec3 ndcCorners[8] = {
        {-1,-1, 0},{1,-1, 0},{1,1, 0},{-1,1, 0},   // near (z = 0)
        {-1,-1, 1},{1,-1, 1},{1,1, 1},{-1,1, 1}    // far  (z = 1)
    };
    glm::vec3 fullCorners[8];
    for (int i = 0; i < 8; ++i) {
        glm::vec4 ws = invVP * glm::vec4(ndcCorners[i], 1.0f);
        fullCorners[i] = glm::vec3(ws) / ws.w;
    }

    // ── Step 3: Interpolate to the cascade sub-slice ───────────────────────
    // fullCorners[0–3] = world positions at cameraNear
    // fullCorners[4–7] = world positions at cameraFar
    float range = cameraFar - cameraNear;
    float tNear = glm::clamp((nearSlice - cameraNear) / range, 0.0f, 1.0f);
    float tFar  = glm::clamp((farSlice  - cameraNear) / range, 0.0f, 1.0f);

    glm::vec3 subCorners[8];
    for (int i = 0; i < 4; ++i) {
        glm::vec3 ray = fullCorners[i + 4] - fullCorners[i];
        subCorners[i]     = fullCorners[i] + ray * tNear;  // cascade near
        subCorners[i + 4] = fullCorners[i] + ray * tFar;   // cascade far
    }

    // ── Step 4: Light-space AABB ────────────────────────────────────────────
    glm::vec3 center(0.0f);
    for (auto& c : subCorners) center += c;
    center /= 8.0f;

    glm::vec3 up = std::abs(glm::dot(lightDir, glm::vec3(0, 1, 0))) < 0.99f
                   ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
    glm::mat4 lightView = glm::lookAt(center + lightDir, center, up);

    glm::vec3 minLS( 1e9f), maxLS(-1e9f);
    for (auto& c : subCorners) {
        glm::vec3 ls = glm::vec3(lightView * glm::vec4(c, 1.0f));
        minLS = glm::min(minLS, ls);
        maxLS = glm::max(maxLS, ls);
    }

    // Extend Z to capture shadow casters outside the visible frustum
    float zExt = (maxLS.z - minLS.z) * 0.5f;
    minLS.z -= zExt;
    maxLS.z += zExt;

    // Small XY margin (3 %) to avoid edge artefacts
    float xM = (maxLS.x - minLS.x) * 0.03f;
    float yM = (maxLS.y - minLS.y) * 0.03f;
    minLS.x -= xM;  maxLS.x += xM;
    minLS.y -= yM;  maxLS.y += yM;

    // ── Step 5: Orthographic projection (Vulkan depth [0, 1]) ──────────────
    // glm::ortho produces OpenGL NDC z in [-1,1], so remap to [0,1] manually.
    glm::mat4 lightProj = glm::ortho(minLS.x, maxLS.x,
                                      minLS.y, maxLS.y,
                                      -maxLS.z, -minLS.z);
    lightProj[1][1] *= -1.0f;          // Vulkan Y flip
    lightProj[2][2] *= 0.5f;           // remap z: [-1,1] → [0,1]
    lightProj[3][2]  = lightProj[3][2] * 0.5f + 0.5f;

    return lightProj * lightView;
}

void ShadowRenderer::updateLightMatrices(const glm::vec3& lightDir,
                                          const glm::vec3& /*cameraPos*/,
                                          const glm::mat4& view,
                                          const glm::mat4& proj,
                                          float near, float far) {
    // PSSM split scheme (λ = 0.75), clamped to CSM max range
    float csmFar = std::min(far, 1000.0f);
    float splits[NUM_CASCADES];
    for (uint32_t i = 0; i < NUM_CASCADES; ++i) {
        float p          = static_cast<float>(i + 1) / static_cast<float>(NUM_CASCADES);
        float logSplit   = near * std::pow(csmFar / near, p);
        float unifSplit  = near + (csmFar - near) * p;
        splits[i]        = 0.75f * logSplit + 0.25f * unifSplit;
    }
    m_cascadeSplits = glm::vec4(splits[0], splits[1], splits[2], splits[3]);

    float prevSplit = near;
    for (uint32_t i = 0; i < NUM_CASCADES; ++i) {
        m_lightSpaceMatrices[i] = computeCascadeMatrix(
            glm::normalize(lightDir), view, proj, prevSplit, splits[i]);
        prevSplit = splits[i];
    }
}

// ---------------------------------------------------------------------------
// Per-frame rendering
// ---------------------------------------------------------------------------

rhi::RHIRenderPassEncoder* ShadowRenderer::beginShadowPass(rhi::RHICommandEncoder* encoder,
                                                            uint32_t frameIndex,
                                                            uint32_t cascadeIndex) {
    if (!m_initialized || !encoder || cascadeIndex >= NUM_CASCADES) return nullptr;

    uint32_t fi = frameIndex % MAX_FRAMES_IN_FLIGHT;

    // Update UBO for this cascade
    LightSpaceUBO ubo;
    ubo.lightSpaceMatrix = m_lightSpaceMatrices[cascadeIndex];
    m_uniformBuffers[fi][cascadeIndex]->write(&ubo, sizeof(LightSpaceUBO));

    rhi::RenderPassDesc passDesc;
    passDesc.width  = SHADOW_MAP_SIZE;
    passDesc.height = SHADOW_MAP_SIZE;
    passDesc.label  = "ShadowPass";

    rhi::RenderPassDepthStencilAttachment depthAtt;
    depthAtt.view            = m_cascadeViews[cascadeIndex].get();
    depthAtt.depthLoadOp     = rhi::LoadOp::Clear;
    depthAtt.depthStoreOp    = rhi::StoreOp::Store;
    depthAtt.depthClearValue = 1.0f;
    depthAtt.depthReadOnly   = false;
    passDesc.depthStencilAttachment = &depthAtt;

#ifdef __linux__
    passDesc.nativeRenderPass  = m_nativeRenderPass;
    passDesc.nativeFramebuffer = m_nativeFramebuffers[cascadeIndex];
#endif

    m_currentRenderPass = encoder->beginRenderPass(passDesc);
    if (!m_currentRenderPass) {
        std::cerr << "[ShadowRenderer] Failed to begin cascade " << cascadeIndex << " pass\n";
        return nullptr;
    }

    m_currentRenderPass->setViewport(0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 0.0f, 1.0f);
    m_currentRenderPass->setScissorRect(0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
    m_currentRenderPass->setPipeline(m_pipeline.get());
    m_currentRenderPass->setBindGroup(0, m_bindGroups[fi][cascadeIndex].get());

    return m_currentRenderPass.get();
}

void ShadowRenderer::endShadowPass() {
    if (m_currentRenderPass) {
        m_currentRenderPass->end();
        m_currentRenderPass.reset();
    }
}

// ---------------------------------------------------------------------------
// Linux native Vulkan resources
// ---------------------------------------------------------------------------

#ifdef __linux__
bool ShadowRenderer::createLinuxRenderPass() {
    auto* vulkanDevice = dynamic_cast<RHI::Vulkan::VulkanRHIDevice*>(m_device);
    if (!vulkanDevice) return false;

    VkDevice vkDevice = static_cast<VkDevice>(*vulkanDevice->getVkDevice());

    VkAttachmentDescription depthAtt{};
    depthAtt.format         = VK_FORMAT_D32_SFLOAT;
    depthAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
    depthAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    depthAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAtt.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 0;
    depthRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 0;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dep.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments    = &depthAtt;
    rpInfo.subpassCount    = 1;
    rpInfo.pSubpasses      = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies   = &dep;

    VkResult res = vkCreateRenderPass(vkDevice, &rpInfo, nullptr, &m_nativeRenderPass);
    if (res != VK_SUCCESS) { std::cerr << "[ShadowRenderer] Linux render pass creation failed\n"; return false; }
    return true;
}

bool ShadowRenderer::createLinuxFramebuffers() {
    auto* vulkanDevice = dynamic_cast<RHI::Vulkan::VulkanRHIDevice*>(m_device);
    if (!vulkanDevice) return false;
    VkDevice vkDevice = static_cast<VkDevice>(*vulkanDevice->getVkDevice());

    for (uint32_t c = 0; c < NUM_CASCADES; ++c) {
        auto* vulkanView = dynamic_cast<RHI::Vulkan::VulkanRHITextureView*>(m_cascadeViews[c].get());
        if (!vulkanView) return false;

        VkImageView depthView = static_cast<VkImageView>(vulkanView->getVkImageView());

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass      = m_nativeRenderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments    = &depthView;
        fbInfo.width           = SHADOW_MAP_SIZE;
        fbInfo.height          = SHADOW_MAP_SIZE;
        fbInfo.layers          = 1;

        VkResult res = vkCreateFramebuffer(vkDevice, &fbInfo, nullptr, &m_nativeFramebuffers[c]);
        if (res != VK_SUCCESS) {
            std::cerr << "[ShadowRenderer] Failed to create framebuffer for cascade " << c << "\n";
            return false;
        }
    }
    return true;
}
#endif

} // namespace rendering
