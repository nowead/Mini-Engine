#include "ShadowRenderer.hpp"
#include "src/utils/FileUtils.hpp"
#include "src/utils/Vertex.hpp"
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
    desc.addressModeU   = rhi::AddressMode::ClampToEdge;
    desc.addressModeV   = rhi::AddressMode::ClampToEdge;
    desc.addressModeW   = rhi::AddressMode::ClampToEdge;
    desc.label          = "ShadowSampler";
#ifndef __EMSCRIPTEN__
    // Vulkan: hardware PCF comparison sampler. Linear filtering makes each
    // sample2DArrayShadow tap a bilinear 2x2 depth comparison, so the 3x3 PCF
    // loop in deferred_lighting.frag.glsl effectively filters a 6x6 area —
    // smoothing the shadow-map grid/stair-stepping. compareOp LessOrEqual:
    // returns the lit fraction (fragment depth <= stored occluder depth).
    desc.magFilter     = rhi::FilterMode::Linear;
    desc.minFilter     = rhi::FilterMode::Linear;
    desc.mipmapFilter  = rhi::MipmapMode::Nearest;
    desc.compareEnable = true;
    desc.compareOp     = rhi::CompareOp::LessOrEqual;
#else
    // WebGPU path keeps a plain Nearest sampler; deferred_lighting.wgsl does its
    // own manual comparison (no sampler_comparison wiring here yet).
    desc.magFilter     = rhi::FilterMode::Nearest;
    desc.minFilter     = rhi::FilterMode::Nearest;
    desc.mipmapFilter  = rhi::MipmapMode::Nearest;
    desc.compareEnable = false;
#endif

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

    // Vertex layout MUST match the engine Vertex struct (pos / normal / texCoord
    // / tangent). The shadow shader consumes only pos but the buffer is shared
    // with the building/G-Buffer pipeline, so we declare every attribute.
    rhi::VertexBufferLayout vertexLayout;
    vertexLayout.stride    = sizeof(Vertex);
    vertexLayout.inputRate = rhi::VertexInputRate::Vertex;
    vertexLayout.attributes = {
        rhi::VertexAttribute(0, 0, rhi::TextureFormat::RGB32Float, offsetof(Vertex, pos)),
        rhi::VertexAttribute(1, 0, rhi::TextureFormat::RGB32Float, offsetof(Vertex, normal)),
        rhi::VertexAttribute(2, 0, rhi::TextureFormat::RG32Float,  offsetof(Vertex, texCoord)),
        rhi::VertexAttribute(3, 0, rhi::TextureFormat::RGB32Float, offsetof(Vertex, tangent))
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
// Shadow matrix computation — single scene-fit map
// ---------------------------------------------------------------------------

/**
 * Build ONE light-space matrix that tightly fits the whole scene's bounding
 * sphere (centre = world origin for this showcase, radius = sceneRadius).
 *
 * This deliberately replaces the previous 4-cascade CSM. CSM refits the
 * shadow frustum to the *camera* frustum every frame, which made the shadow
 * swim, pulse, and balloon with zoom and tangled WebGPU/Vulkan NDC
 * conventions. For a small, fixed scene a single camera-independent map is
 * exactly correct and inherently stable: the matrix is constant frame-to-
 * frame regardless of camera zoom/orbit, so there is no swimming and no
 * cascade-selection logic to get wrong.
 *
 * cameraView/cameraProj/nearSlice/farSlice are unused (kept so the .hpp
 * signature and the per-cascade call sites stay unchanged — all four UBO
 * slots receive this same matrix).
 */
glm::mat4 ShadowRenderer::computeCascadeMatrix(const glm::vec3& lightDir,
                                                const glm::mat4& /*cameraView*/,
                                                const glm::mat4& /*cameraProj*/,
                                                float /*nearSlice*/,
                                                float /*farSlice*/,
                                                float sceneRadius) {
    const glm::vec3 sceneCenter(0.0f);                 // showcase grid is origin-centred
    const float Rc = std::max(sceneRadius, 1.0f);      // building-cluster radius
    const glm::vec3 L = glm::normalize(lightDir);      // points toward the sun

    // The ortho must contain not just the casters but the long shadows they
    // throw onto the ground. The sun is very low ("low-angle sunset",
    // sunDir.y ≈ 0.28), so a building of height H casts a shadow of length
    // ~ H·|L_horizontal| / L.y  (≈ 3–4× H here). Sizing the box to the
    // cluster radius alone clipped those shadows, making them look the wrong
    // size/shape. Expand the half-extent to cover cluster + shadow throw.
    const float horiz   = std::sqrt(std::max(1.0f - L.y * L.y, 1e-4f));
    const float stretch = horiz / std::max(std::abs(L.y), 0.05f);   // shadow/height
    // Cluster radius doubles as a proxy for the tallest caster's reach.
    const float R = Rc + Rc * std::clamp(stretch, 0.0f, 6.0f);

    glm::vec3 up = std::abs(glm::dot(L, glm::vec3(0, 1, 0))) < 0.99f
                   ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);

    // Light "camera" sits on the sun side, 3R from the scene centre, looking
    // back along the light-travel direction (-L). Depth window [0, 6R] keeps
    // the box (occupying [2R, 4R] from the eye) well inside with a tail for
    // tall casters.
    glm::mat4 lightView = glm::lookAt(sceneCenter + L * (R * 3.0f),
                                      sceneCenter, up);

    // glm::ortho gives OpenGL NDC z ∈ [-1,1]; remap to [0,1] for WebGPU/Vulkan.
    // Keep the Vulkan Y-flip: it is consistent with shadow.wgsl (write) and
    // deferred_lighting.wgsl (read, projCoords.y = -y*0.5+0.5).
    glm::mat4 lightProj = glm::ortho(-R, R, -R, R, 0.0f, R * 6.0f);
    lightProj[1][1] *= -1.0f;
    lightProj[2][2] *= 0.5f;
    lightProj[3][2]  = lightProj[3][2] * 0.5f + 0.5f;

    return lightProj * lightView;
}

void ShadowRenderer::updateLightMatrices(const glm::vec3& lightDir,
                                          const glm::vec3& /*cameraPos*/,
                                          const glm::mat4& view,
                                          const glm::mat4& proj,
                                          float /*near*/, float /*far*/,
                                          float sceneRadius) {
    // Single scene-fit matrix, replicated into all four UBO slots so no C++
    // struct / binding / Vulkan-path change is needed. cascadeSplits set huge
    // so the shader's cascade pick always resolves to slot 0 (all identical).
    glm::mat4 m = computeCascadeMatrix(glm::normalize(lightDir), view, proj,
                                       0.0f, 0.0f, sceneRadius);
    for (uint32_t i = 0; i < NUM_CASCADES; ++i)
        m_lightSpaceMatrices[i] = m;
    m_cascadeSplits = glm::vec4(1.0e18f);
}

// ---------------------------------------------------------------------------
// Per-frame rendering
// ---------------------------------------------------------------------------

std::unique_ptr<rhi::RHIRenderPassEncoder> ShadowRenderer::beginShadowPass(
    rhi::RHICommandEncoder* encoder, uint32_t frameIndex, uint32_t cascadeIndex) {
    if (!m_initialized || !encoder || cascadeIndex >= NUM_CASCADES) return nullptr;

    // D3-2: this method touches only per-cascade state (m_uniformBuffers[fi][c],
    // m_cascadeViews[c], m_bindGroups[fi][c], m_lightSpaceMatrices[c]) plus
    // read-only shared handles (m_pipeline). It no longer stores the render-pass
    // encoder in a member, so different cascades may run it concurrently on worker
    // threads. The caller owns the returned encoder and ends it (end() or dtor).

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

    auto pass = encoder->beginRenderPass(passDesc);
    if (!pass) {
        std::cerr << "[ShadowRenderer] Failed to begin cascade " << cascadeIndex << " pass\n";
        return nullptr;
    }

    pass->setViewport(0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 0.0f, 1.0f);
    pass->setScissorRect(0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
    pass->setPipeline(m_pipeline.get());
    pass->setBindGroup(0, m_bindGroups[fi][cascadeIndex].get());

    return pass;
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
