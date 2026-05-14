#include "DeferredLightingPass.hpp"
#include "src/utils/FileUtils.hpp"
#include <iostream>

namespace rendering {

DeferredLightingPass::DeferredLightingPass(rhi::RHIDevice* device) : m_device(device) {}

bool DeferredLightingPass::initialize(
    const std::array<rhi::RHIBuffer*, MAX_FRAMES_IN_FLIGHT>& uniformBuffers,
    size_t uboSize,
    rhi::RHITextureView* gBuffer0View,
    rhi::RHITextureView* gBuffer1View,
    rhi::RHITextureView* gBuffer2View,
    rhi::RHITextureView* depthView,
    rhi::RHISampler*     gbufferSampler,
    rhi::RHITextureView* shadowCsmView,
    rhi::RHISampler*     shadowSampler,
    rhi::RHITextureView* irradianceView,
    rhi::RHITextureView* prefilteredView,
    rhi::RHITextureView* brdfLUTView,
    rhi::RHISampler*     iblSampler,
    void*                nativeRenderPass) {

    if (!createPipeline(nativeRenderPass)) {
        std::cerr << "[DeferredLightingPass] createPipeline failed\n";
        return false;
    }
    if (!createBindGroups(uniformBuffers, uboSize,
                          gBuffer0View, gBuffer1View, gBuffer2View, depthView,
                          gbufferSampler, shadowCsmView, shadowSampler,
                          irradianceView, prefilteredView, brdfLUTView, iblSampler)) {
        std::cerr << "[DeferredLightingPass] createBindGroups failed\n";
        return false;
    }

    m_initialized = true;
    std::cout << "[DeferredLightingPass] Initialized\n";
    return true;
}

bool DeferredLightingPass::createPipeline(void* nativeRenderPass) {
#ifdef __EMSCRIPTEN__
    {
        auto raw = FileUtils::readFile("shaders/deferred_lighting.wgsl");
        if (raw.empty()) { std::cerr << "[DeferredLightingPass] Failed to load deferred_lighting.wgsl\n"; return false; }
        std::vector<uint8_t> code(raw.begin(), raw.end());
        rhi::ShaderSource vsSrc(rhi::ShaderLanguage::WGSL, code, rhi::ShaderStage::Vertex,   "vs_main");
        rhi::ShaderSource fsSrc(rhi::ShaderLanguage::WGSL, code, rhi::ShaderStage::Fragment, "fs_main");
        m_vertexShader   = m_device->createShader(rhi::ShaderDesc(vsSrc, "DeferredLightingVS"));
        m_fragmentShader = m_device->createShader(rhi::ShaderDesc(fsSrc, "DeferredLightingFS"));
    }
#else
    {
        auto loadSpv = [&](const char* path, rhi::ShaderStage stage, const char* label) {
            auto raw = FileUtils::readFile(path);
            if (raw.empty()) { std::cerr << "[DeferredLightingPass] Failed to load " << path << "\n"; return std::unique_ptr<rhi::RHIShader>{}; }
            std::vector<uint8_t> code(raw.begin(), raw.end());
            rhi::ShaderSource src(rhi::ShaderLanguage::SPIRV, code, stage, "main");
            return m_device->createShader(rhi::ShaderDesc(src, label));
        };
        m_vertexShader   = loadSpv("shaders/deferred_lighting.vert.spv", rhi::ShaderStage::Vertex,   "DeferredLightingVS");
        m_fragmentShader = loadSpv("shaders/deferred_lighting.frag.spv", rhi::ShaderStage::Fragment, "DeferredLightingFS");
    }
#endif
    if (!m_vertexShader || !m_fragmentShader) return false;

    // Bind group layout: 12 bindings in set 0.
    // Depth textures (bindings 4, 6) need DepthTexture type on WebGPU (texture_depth_2d / _2d_array).
    // Shadow CSM (binding 6) is a 2D array; cubemaps (8, 9) need ViewCube dimension.
    rhi::BindGroupLayoutDesc layoutDesc;
    using S = rhi::ShaderStage;
    using T = rhi::BindingType;
    using D = rhi::TextureViewDimension;

    auto entry = [](uint32_t b, S s, T t, D dim = D::View2D) {
        rhi::BindGroupLayoutEntry e(b, s, t);
        e.textureViewDimension = dim;
        return e;
    };

#ifdef __EMSCRIPTEN__
    layoutDesc.entries = {
        entry(0,  S::Fragment, T::UniformBuffer),
        entry(1,  S::Fragment, T::SampledTexture),
        entry(2,  S::Fragment, T::SampledTexture),
        entry(3,  S::Fragment, T::SampledTexture),
        entry(4,  S::Fragment, T::DepthTexture),                           // texture_depth_2d
        entry(5,  S::Fragment, T::NonFilteringSampler),                    // gbufferSampler (nearest)
        entry(6,  S::Fragment, T::DepthTexture,  D::View2DArray),          // texture_depth_2d_array
        entry(7,  S::Fragment, T::NonFilteringSampler),                    // shadowSampler (nearest) — depth+textureSample requires non-filtering
        entry(8,  S::Fragment, T::SampledTexture, D::ViewCube),            // irradiance cube
        entry(9,  S::Fragment, T::SampledTexture, D::ViewCube),            // prefiltered cube
        entry(10, S::Fragment, T::SampledTexture),
        entry(11, S::Fragment, T::Sampler),                                // iblSampler (linear)
    };
#else
    layoutDesc.entries = {
        entry(0,  S::Fragment, T::UniformBuffer),
        entry(1,  S::Fragment, T::SampledTexture),
        entry(2,  S::Fragment, T::SampledTexture),
        entry(3,  S::Fragment, T::SampledTexture),
        entry(4,  S::Fragment, T::SampledTexture),                 // depth as sampled on Vulkan
        entry(5,  S::Fragment, T::Sampler),
        entry(6,  S::Fragment, T::SampledTexture, D::View2DArray), // CSM array
        entry(7,  S::Fragment, T::Sampler),
        entry(8,  S::Fragment, T::SampledTexture, D::ViewCube),
        entry(9,  S::Fragment, T::SampledTexture, D::ViewCube),
        entry(10, S::Fragment, T::SampledTexture),
        entry(11, S::Fragment, T::Sampler),
    };
#endif
    layoutDesc.label = "DeferredLightingBGLayout";

    m_bindGroupLayout = m_device->createBindGroupLayout(layoutDesc);
    if (!m_bindGroupLayout) return false;

    rhi::PipelineLayoutDesc plDesc;
    plDesc.bindGroupLayouts.push_back(m_bindGroupLayout.get());
    plDesc.label = "DeferredLightingPipelineLayout";
    m_pipelineLayout = m_device->createPipelineLayout(plDesc);
    if (!m_pipelineLayout) return false;

    rhi::RenderPipelineDesc pipelineDesc;
    pipelineDesc.label          = "DeferredLightingPipeline";
    pipelineDesc.layout         = m_pipelineLayout.get();
    pipelineDesc.vertexShader   = m_vertexShader.get();
    pipelineDesc.fragmentShader = m_fragmentShader.get();

    // No vertex buffers — fullscreen triangle via gl_VertexIndex
    pipelineDesc.primitive.topology  = rhi::PrimitiveTopology::TriangleList;
    pipelineDesc.primitive.cullMode  = rhi::CullMode::None;
    pipelineDesc.primitive.frontFace = rhi::FrontFace::CounterClockwise;

    // Single HDR color target (no depth write)
    rhi::ColorTargetState ct;
    ct.format = rhi::TextureFormat::RGBA16Float;
    pipelineDesc.colorTargets = { ct };

    // No depth write — reads depth as a sampled texture
    pipelineDesc.depthStencil = nullptr;

    pipelineDesc.nativeRenderPass = nativeRenderPass;

    m_pipeline = m_device->createRenderPipeline(pipelineDesc);
    return m_pipeline != nullptr;
}

bool DeferredLightingPass::createBindGroups(
    const std::array<rhi::RHIBuffer*, MAX_FRAMES_IN_FLIGHT>& uniformBuffers,
    size_t uboSize,
    rhi::RHITextureView* gBuffer0View,
    rhi::RHITextureView* gBuffer1View,
    rhi::RHITextureView* gBuffer2View,
    rhi::RHITextureView* depthView,
    rhi::RHISampler*     gbufferSampler,
    rhi::RHITextureView* shadowCsmView,
    rhi::RHISampler*     shadowSampler,
    rhi::RHITextureView* irradianceView,
    rhi::RHITextureView* prefilteredView,
    rhi::RHITextureView* brdfLUTView,
    rhi::RHISampler*     iblSampler) {

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        rhi::BindGroupDesc desc;
        desc.layout = m_bindGroupLayout.get();
        desc.entries = {
            rhi::BindGroupEntry::Buffer (0, uniformBuffers[i], 0, uboSize),
            rhi::BindGroupEntry::TextureView(1, gBuffer0View),
            rhi::BindGroupEntry::TextureView(2, gBuffer1View),
            rhi::BindGroupEntry::TextureView(3, gBuffer2View),
            rhi::BindGroupEntry::TextureView(4, depthView),
            rhi::BindGroupEntry::Sampler(5, gbufferSampler),
            rhi::BindGroupEntry::TextureView(6, shadowCsmView),
            rhi::BindGroupEntry::Sampler(7, shadowSampler),
            rhi::BindGroupEntry::TextureView(8, irradianceView),
            rhi::BindGroupEntry::TextureView(9, prefilteredView),
            rhi::BindGroupEntry::TextureView(10, brdfLUTView),
            rhi::BindGroupEntry::Sampler(11, iblSampler),
        };
        desc.label = "DeferredLightingBindGroup";

        m_bindGroups[i] = m_device->createBindGroup(desc);
        if (!m_bindGroups[i]) {
            std::cerr << "[DeferredLightingPass] Failed to create bind group " << i << "\n";
            return false;
        }
    }
    return true;
}

} // namespace rendering
