#include "GBufferPass.hpp"
#include "src/utils/FileUtils.hpp"
#include "src/utils/Vertex.hpp"
#include <iostream>

#ifndef __EMSCRIPTEN__
#include <rhi/vulkan/VulkanRHIDevice.hpp>
#include <rhi/vulkan/VulkanRHITexture.hpp>
#include <rhi/vulkan/VulkanRHICommandEncoder.hpp>
#include <vulkan/vulkan.h>
#endif

namespace rendering {

GBufferPass::GBufferPass(rhi::RHIDevice* device) : m_device(device) {}

GBufferPass::~GBufferPass() {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    if (m_device) {
        auto* vd = dynamic_cast<RHI::Vulkan::VulkanRHIDevice*>(m_device);
        if (vd) {
            VkDevice vkDev = static_cast<VkDevice>(*vd->getVkDevice());
            destroyLinuxFramebuffer();
            if (m_nativeRenderPass != VK_NULL_HANDLE) {
                vkDestroyRenderPass(vkDev, m_nativeRenderPass, nullptr);
                m_nativeRenderPass = VK_NULL_HANDLE;
            }
        }
    }
#endif
}

bool GBufferPass::initialize(uint32_t width, uint32_t height,
                              rhi::RHIBindGroupLayout* buildingBGLayout,
                              rhi::RHIBindGroupLayout* ssboLayout,
                              rhi::RHITextureView* depthView,
                              VkDescriptorSetLayout bindlessLayout,
                              rhi::RHIBindGroupLayout* materialLayout) {
    m_depthView = depthView;
#ifndef __EMSCRIPTEN__
    m_bindlessLayout = bindlessLayout;
#endif
    m_materialLayout = materialLayout;  // WebGPU only; nullptr elsewhere

    if (!createTextures(width, height)) { std::cerr << "[GBufferPass] createTextures failed\n"; return false; }
    if (!createSampler())               { std::cerr << "[GBufferPass] createSampler failed\n";  return false; }

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    if (!createLinuxRenderPass())  { std::cerr << "[GBufferPass] createLinuxRenderPass failed\n";  return false; }
    if (!createLinuxFramebuffer()) { std::cerr << "[GBufferPass] createLinuxFramebuffer failed\n"; return false; }
#endif

    if (!createPipeline(buildingBGLayout, ssboLayout, bindlessLayout)) {
        std::cerr << "[GBufferPass] createPipeline failed\n"; return false;
    }

    m_initialized = true;
    std::cout << "[GBufferPass] Initialized " << width << "x" << height
              << (bindlessLayout ? " (bindless enabled)" : " (bindless disabled)") << "\n";
    return true;
}

void GBufferPass::resize(uint32_t width, uint32_t height,
                          rhi::RHITextureView* newDepthView) {
    m_depthView = newDepthView;
    m_gBuffer0.reset(); m_gBuffer0View.reset();
    m_gBuffer1.reset(); m_gBuffer1View.reset();
    m_gBuffer2.reset(); m_gBuffer2View.reset();
    m_gBuffer3.reset(); m_gBuffer3View.reset();

    if (!createTextures(width, height)) {
        std::cerr << "[GBufferPass] resize: createTextures failed\n";
        m_initialized = false;
        return;
    }

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    destroyLinuxFramebuffer();
    createLinuxFramebuffer();
#endif
}

// ---------------------------------------------------------------------------
// Resource creation
// ---------------------------------------------------------------------------

bool GBufferPass::createTextures(uint32_t width, uint32_t height) {
    auto makeTexture = [&](rhi::TextureFormat fmt, const char* label,
                           std::unique_ptr<rhi::RHITexture>& tex,
                           std::unique_ptr<rhi::RHITextureView>& view) -> bool {
        rhi::TextureDesc desc;
        desc.size   = rhi::Extent3D(width, height, 1);
        desc.format = fmt;
        desc.usage  = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled
                    | rhi::TextureUsage::Storage;
        desc.label  = label;
        tex = m_device->createTexture(desc);
        if (!tex) return false;

        rhi::TextureViewDesc viewDesc;
        viewDesc.format    = fmt;
        viewDesc.dimension = rhi::TextureViewDimension::View2D;
        viewDesc.label     = label;
        view = tex->createView(viewDesc);
        return view != nullptr;
    };

    bool ok = makeTexture(rhi::TextureFormat::RGBA16Float, "GBuffer0", m_gBuffer0, m_gBuffer0View)
           && makeTexture(rhi::TextureFormat::RGBA8Unorm,  "GBuffer1", m_gBuffer1, m_gBuffer1View)
           && makeTexture(rhi::TextureFormat::RGBA8Unorm,  "GBuffer2", m_gBuffer2, m_gBuffer2View);
#ifndef __EMSCRIPTEN__
    // Target 3 = screen-space velocity for TAA (Vulkan-first; the WGSL G-Buffer
    // still writes 3 targets, so the WebGPU pipeline stays 3-target for now).
    ok = ok && makeTexture(rhi::TextureFormat::RG16Float, "GBuffer3", m_gBuffer3, m_gBuffer3View);
#endif
    return ok;
}

bool GBufferPass::createSampler() {
    rhi::SamplerDesc desc;
    desc.magFilter    = rhi::FilterMode::Nearest;
    desc.minFilter    = rhi::FilterMode::Nearest;
    desc.mipmapFilter = rhi::MipmapMode::Nearest;
    desc.addressModeU = rhi::AddressMode::ClampToEdge;
    desc.addressModeV = rhi::AddressMode::ClampToEdge;
    desc.addressModeW = rhi::AddressMode::ClampToEdge;
    desc.label        = "GBufferSampler";
    m_sampler = m_device->createSampler(desc);
    return m_sampler != nullptr;
}

bool GBufferPass::createPipeline(rhi::RHIBindGroupLayout* buildingBGLayout,
                                  rhi::RHIBindGroupLayout* ssboLayout,
                                  VkDescriptorSetLayout bindlessLayout) {
    // ------------------------------------------------------------------
    // Shader loading: WGSL on WebGPU, SPIR-V on Vulkan
    // ------------------------------------------------------------------
#ifdef __EMSCRIPTEN__
    {
        auto wgslRaw = FileUtils::readFile("shaders/gbuffer.wgsl");
        if (wgslRaw.empty()) { std::cerr << "[GBufferPass] Failed to load gbuffer.wgsl\n"; return false; }
        std::vector<uint8_t> code(wgslRaw.begin(), wgslRaw.end());

        rhi::ShaderSource vsSrc(rhi::ShaderLanguage::WGSL, code, rhi::ShaderStage::Vertex,   "vs_main");
        rhi::ShaderSource fsSrc(rhi::ShaderLanguage::WGSL, code, rhi::ShaderStage::Fragment, "fs_main");
        m_vertexShader   = m_device->createShader(rhi::ShaderDesc(vsSrc, "GBufferVS"));
        m_fragmentShader = m_device->createShader(rhi::ShaderDesc(fsSrc, "GBufferFS"));
    }
#else
    {
        auto loadSpv = [&](const char* path, rhi::ShaderStage stage, const char* label) {
            auto raw = FileUtils::readFile(path);
            if (raw.empty()) { std::cerr << "[GBufferPass] Failed to load " << path << "\n"; return std::unique_ptr<rhi::RHIShader>{}; }
            std::vector<uint8_t> code(raw.begin(), raw.end());
            rhi::ShaderSource src(rhi::ShaderLanguage::SPIRV, code, stage, "main");
            return m_device->createShader(rhi::ShaderDesc(src, label));
        };

        const char* fragSpvPath = bindlessLayout
            ? "shaders/gbuffer.frag.spv"
            : "shaders/gbuffer_nobindless.frag.spv";
        m_vertexShader   = loadSpv("shaders/gbuffer.vert.spv", rhi::ShaderStage::Vertex,   "GBufferVS");
        m_fragmentShader = loadSpv(fragSpvPath,                  rhi::ShaderStage::Fragment, "GBufferFS");
    }
#endif
    if (!m_vertexShader || !m_fragmentShader) return false;

    // ------------------------------------------------------------------
    // Pipeline layout
    // ------------------------------------------------------------------
    rhi::PipelineLayoutDesc layoutDesc;
    if (buildingBGLayout) layoutDesc.bindGroupLayouts.push_back(buildingBGLayout);
    if (ssboLayout)       layoutDesc.bindGroupLayouts.push_back(ssboLayout);
#ifndef __EMSCRIPTEN__
    if (bindlessLayout)   layoutDesc.nativeExtraSetLayouts.push_back(bindlessLayout);
#else
    // WebGPU set 2 = per-material textures (baseColor / normal / MR / emissive + sampler).
    // Native Vulkan uses bindless at set 2 instead.
    if (m_materialLayout) layoutDesc.bindGroupLayouts.push_back(m_materialLayout);
#endif
    layoutDesc.label = "GBufferPipelineLayout";
    m_pipelineLayout = m_device->createPipelineLayout(layoutDesc);
    if (!m_pipelineLayout) return false;

    // ------------------------------------------------------------------
    // Render pipeline descriptor
    // ------------------------------------------------------------------
    rhi::RenderPipelineDesc pipelineDesc;
    pipelineDesc.label          = "GBufferPipeline";
    pipelineDesc.layout         = m_pipelineLayout.get();
    pipelineDesc.vertexShader   = m_vertexShader.get();
    pipelineDesc.fragmentShader = m_fragmentShader.get();

    // Vertex layout: matches engine Vertex (pos / normal / texCoord / tangent).
    // Tangent (location 3) is consumed by sub-task 3 onward for normal mapping;
    // declared here so the buffer/shader layout stays consistent across
    // pipelines that all bind the same mesh data.
    rhi::VertexBufferLayout vbl;
    vbl.stride    = sizeof(Vertex);
    vbl.inputRate = rhi::VertexInputRate::Vertex;
    vbl.attributes = {
        rhi::VertexAttribute(0, 0, rhi::TextureFormat::RGB32Float, offsetof(Vertex, pos)),
        rhi::VertexAttribute(1, 0, rhi::TextureFormat::RGB32Float, offsetof(Vertex, normal)),
        rhi::VertexAttribute(2, 0, rhi::TextureFormat::RG32Float,  offsetof(Vertex, texCoord)),
        rhi::VertexAttribute(3, 0, rhi::TextureFormat::RGB32Float, offsetof(Vertex, tangent))
    };
    pipelineDesc.vertex.buffers.push_back(vbl);

    pipelineDesc.primitive.topology  = rhi::PrimitiveTopology::TriangleList;
    pipelineDesc.primitive.cullMode  = rhi::CullMode::Back;
    pipelineDesc.primitive.frontFace = rhi::FrontFace::Clockwise;

    // MRT color targets. Native adds target 3 (RG16Float velocity) for TAA;
    // WebGPU keeps 3 (its WGSL fragment writes 3 outputs).
    rhi::ColorTargetState ct0; ct0.format = rhi::TextureFormat::RGBA16Float;
    rhi::ColorTargetState ct1; ct1.format = rhi::TextureFormat::RGBA8Unorm;
    rhi::ColorTargetState ct2; ct2.format = rhi::TextureFormat::RGBA8Unorm;
    pipelineDesc.colorTargets = { ct0, ct1, ct2 };
#ifndef __EMSCRIPTEN__
    rhi::ColorTargetState ct3; ct3.format = rhi::TextureFormat::RG16Float;
    pipelineDesc.colorTargets.push_back(ct3);
#endif

    // Depth
    rhi::DepthStencilState depthState;
    depthState.depthTestEnabled  = true;
    depthState.depthWriteEnabled = true;
    depthState.depthCompare      = rhi::CompareOp::Less;
    depthState.format            = rhi::TextureFormat::Depth32Float;
    pipelineDesc.depthStencil    = &depthState;

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    pipelineDesc.nativeRenderPass = m_nativeRenderPass;
#endif

    m_pipeline = m_device->createRenderPipeline(pipelineDesc);
    return m_pipeline != nullptr;
}

// ---------------------------------------------------------------------------
// Execute
// ---------------------------------------------------------------------------

void GBufferPass::execute(rhi::RHICommandEncoder* encoder,
                           rhi::RHIBindGroup*  bindGroup0,
                           rhi::RHIBindGroup*  ssboBindGroup,
                           rhi::RHIBuffer*     vertexBuffer,
                           rhi::RHIBuffer*     indexBuffer,
                           rhi::RHIBuffer*     indirectBuffer,
                           uint32_t width, uint32_t height,
                           VkDescriptorSet bindlessSet,
                           rhi::RHIBindGroup* defaultMaterialBindGroup,
                           const std::vector<ShowcaseDraw>& showcaseDraws) {
    if (!m_initialized || !encoder) return;

    rhi::RenderPassDesc passDesc;
    passDesc.width  = width;
    passDesc.height = height;
    passDesc.label  = "GBufferPass";

    rhi::RenderPassColorAttachment ca0;
    ca0.view = m_gBuffer0View.get(); ca0.loadOp = rhi::LoadOp::Clear; ca0.storeOp = rhi::StoreOp::Store;
    ca0.clearValue = rhi::ClearColorValue(0.0f, 0.0f, 0.0f, 0.0f);

    rhi::RenderPassColorAttachment ca1;
    ca1.view = m_gBuffer1View.get(); ca1.loadOp = rhi::LoadOp::Clear; ca1.storeOp = rhi::StoreOp::Store;
    ca1.clearValue = rhi::ClearColorValue(0.0f, 0.0f, 0.0f, 0.0f);

    rhi::RenderPassColorAttachment ca2;
    ca2.view = m_gBuffer2View.get(); ca2.loadOp = rhi::LoadOp::Clear; ca2.storeOp = rhi::StoreOp::Store;
    ca2.clearValue = rhi::ClearColorValue(0.0f, 0.0f, 0.0f, 0.0f);

    passDesc.colorAttachments = { ca0, ca1, ca2 };
#ifndef __EMSCRIPTEN__
    rhi::RenderPassColorAttachment ca3;  // velocity; clear to 0 (no motion)
    ca3.view = m_gBuffer3View.get(); ca3.loadOp = rhi::LoadOp::Clear; ca3.storeOp = rhi::StoreOp::Store;
    ca3.clearValue = rhi::ClearColorValue(0.0f, 0.0f, 0.0f, 0.0f);
    passDesc.colorAttachments.push_back(ca3);
#endif

    rhi::RenderPassDepthStencilAttachment depthAtt;
    if (m_depthView) {
        depthAtt.view            = m_depthView;
        depthAtt.depthLoadOp     = rhi::LoadOp::Clear;
        depthAtt.depthStoreOp    = rhi::StoreOp::Store;
        depthAtt.depthClearValue = 1.0f;
        depthAtt.depthReadOnly   = false;
        passDesc.depthStencilAttachment = &depthAtt;
    }

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    passDesc.nativeRenderPass  = m_nativeRenderPass;
    passDesc.nativeFramebuffer = m_nativeFramebuffer;
#endif

    auto pass = encoder->beginRenderPass(passDesc);
    if (!pass) { std::cerr << "[GBufferPass] beginRenderPass failed\n"; return; }

    pass->setViewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f);
    pass->setScissorRect(0, 0, width, height);
    pass->setPipeline(m_pipeline.get());

    if (bindGroup0)    pass->setBindGroup(0, bindGroup0);
    if (ssboBindGroup) pass->setBindGroup(1, ssboBindGroup);

#ifndef __EMSCRIPTEN__
    // Vulkan-only: bind bindless texture array at set 2 via native call.
    if (bindlessSet && m_bindlessLayout) {
        auto* vulkanPass = dynamic_cast<RHI::Vulkan::VulkanRHIRenderPassEncoder*>(pass.get());
        if (vulkanPass) {
            vulkanPass->bindNativeDescriptorSet(2, bindlessSet);
        }
    }
    (void)defaultMaterialBindGroup;
#else
    // WebGPU set 2: per-material PBR texture bind group. Buildings use the
    // default (dummy 1×1 white/flat textures so multiplication is identity);
    // each showcase sub-mesh draw below swaps in its own bind group.
    if (defaultMaterialBindGroup) pass->setBindGroup(2, defaultMaterialBindGroup);
#endif

    if (vertexBuffer && indexBuffer && indirectBuffer) {
        pass->setVertexBuffer(0, vertexBuffer, 0);
        pass->setIndexBuffer(indexBuffer, rhi::IndexFormat::Uint32, 0);
        pass->drawIndexedIndirect(indirectBuffer, 0);
    }

    // Showcase asset draws — one per sub-mesh (node-tree-flattened glTF). Same
    // pipeline + set 0; each swaps set 1 (its own ObjectData / visibleIndices)
    // and, on WebGPU, set 2 (its own material bind group). Native Vulkan keeps
    // the bindless set 2 bound above and reads per-sub-mesh indices from
    // ObjectData. Entries with null buffers or zero indices are skipped.
    for (const ShowcaseDraw& d : showcaseDraws) {
        if (!d.ssboBindGroup || !d.vertexBuffer || !d.indexBuffer || d.indexCount == 0) {
            continue;
        }
        pass->setBindGroup(1, d.ssboBindGroup);
#ifdef __EMSCRIPTEN__
        if (d.materialBindGroup) pass->setBindGroup(2, d.materialBindGroup);
#endif
        pass->setVertexBuffer(0, d.vertexBuffer, 0);
        pass->setIndexBuffer(d.indexBuffer, rhi::IndexFormat::Uint32, 0);
        pass->drawIndexed(d.indexCount, 1, 0, 0, 0);
    }

    pass->end();
}

// ---------------------------------------------------------------------------
// Linux native resources (Vulkan-only, not compiled on EMSCRIPTEN)
// ---------------------------------------------------------------------------

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
bool GBufferPass::createLinuxRenderPass() {
    auto* vd = dynamic_cast<RHI::Vulkan::VulkanRHIDevice*>(m_device);
    if (!vd) return false;
    VkDevice vkDev = static_cast<VkDevice>(*vd->getVkDevice());

    VkAttachmentDescription atts[5]{};

    atts[0].format = VK_FORMAT_R16G16B16A16_SFLOAT; atts[0].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; atts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    atts[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; atts[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    atts[1] = atts[0]; atts[1].format = VK_FORMAT_R8G8B8A8_UNORM;
    atts[2] = atts[1];
    atts[3] = atts[0]; atts[3].format = VK_FORMAT_R16G16_SFLOAT;  // velocity (TAA)

    atts[4].format = VK_FORMAT_D32_SFLOAT; atts[4].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[4].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; atts[4].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    atts[4].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; atts[4].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[4].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; atts[4].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRefs[4] = {
        {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        {3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}
    };
    VkAttachmentReference depthRef = {4, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 4;
    subpass.pColorAttachments    = colorRefs;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 5; rpInfo.pAttachments = atts;
    rpInfo.subpassCount    = 1; rpInfo.pSubpasses   = &subpass;
    rpInfo.dependencyCount = 1; rpInfo.pDependencies = &dep;

    VkResult res = vkCreateRenderPass(vkDev, &rpInfo, nullptr, &m_nativeRenderPass);
    if (res != VK_SUCCESS) { std::cerr << "[GBufferPass] Linux vkCreateRenderPass failed\n"; return false; }
    return true;
}

bool GBufferPass::createLinuxFramebuffer() {
    auto* vd = dynamic_cast<RHI::Vulkan::VulkanRHIDevice*>(m_device);
    if (!vd) return false;
    VkDevice vkDev = static_cast<VkDevice>(*vd->getVkDevice());

    auto getView = [&](rhi::RHITextureView* v) -> VkImageView {
        auto* vv = dynamic_cast<RHI::Vulkan::VulkanRHITextureView*>(v);
        return vv ? static_cast<VkImageView>(vv->getVkImageView()) : VK_NULL_HANDLE;
    };
    VkImageView views[5] = {
        getView(m_gBuffer0View.get()), getView(m_gBuffer1View.get()),
        getView(m_gBuffer2View.get()), getView(m_gBuffer3View.get()),
        getView(m_depthView)
    };
    uint32_t W = 0, H = 0;
    if (m_gBuffer0) { auto sz = m_gBuffer0->getSize(); W = sz.width; H = sz.height; }

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass      = m_nativeRenderPass;
    fbInfo.attachmentCount = 5; fbInfo.pAttachments = views;
    fbInfo.width = W; fbInfo.height = H; fbInfo.layers = 1;

    VkResult res = vkCreateFramebuffer(vkDev, &fbInfo, nullptr, &m_nativeFramebuffer);
    if (res != VK_SUCCESS) { std::cerr << "[GBufferPass] Linux vkCreateFramebuffer failed\n"; return false; }
    return true;
}

void GBufferPass::destroyLinuxFramebuffer() {
    if (m_nativeFramebuffer != VK_NULL_HANDLE && m_device) {
        auto* vd = dynamic_cast<RHI::Vulkan::VulkanRHIDevice*>(m_device);
        if (vd) {
            VkDevice vkDev = static_cast<VkDevice>(*vd->getVkDevice());
            vkDestroyFramebuffer(vkDev, m_nativeFramebuffer, nullptr);
            m_nativeFramebuffer = VK_NULL_HANDLE;
        }
    }
}
#endif

} // namespace rendering
