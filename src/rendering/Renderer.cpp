#include "Renderer.hpp"
#ifndef __EMSCRIPTEN__
#include "src/ui/ImGuiManager.hpp"
#include "src/utils/GpuProfiler.hpp"
#endif
#include "InstancedRenderData.hpp"
#include "src/utils/Logger.hpp"
#include "src/utils/FileUtils.hpp"

// Phase 9: Vulkan-specific includes for platform-specific functionality
// TODO Phase 10: Consider adding getRenderPass() to RHI interface to remove this last dependency
#ifndef __EMSCRIPTEN__
#include <rhi/vulkan/VulkanRHISwapchain.hpp>
#include <rhi/vulkan/VulkanRHICommandEncoder.hpp>
#include <rhi/vulkan/VulkanRHITexture.hpp>
#include <rhi/vulkan/VulkanRHIBuffer.hpp>
#include <rhi/vulkan/VulkanRHIDevice.hpp>
#else
#include <rhi/webgpu/WebGPURHIDevice.hpp>
#include <rhi/webgpu/WebGPURHICommandEncoder.hpp>
#include "src/utils/WebGPUTimer.hpp"
#endif

#include <stdexcept>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// Phase 7: LegacyCommandBufferAdapter removed - ImGui now uses RHI directly

Renderer::Renderer(GLFWwindow* window,
                   const std::vector<const char*>& validationLayers,
                   bool enableValidation)
    : window(window),
      startTime(std::chrono::high_resolution_clock::now()),
      viewMatrix(glm::mat4(1.0f)),
      projectionMatrix(glm::mat4(1.0f)) {

    // Initialize RHI Bridge (handles device creation, surface, and lifecycle)
    rhiBridge = std::make_unique<rendering::RendererBridge>(window, enableValidation);

    // Create swapchain (needed for depth resources)
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    rhiBridge->createSwapchain(static_cast<uint32_t>(width), static_cast<uint32_t>(height), true);

    // Create high-level managers using RHI
    auto* rhiDevice = rhiBridge->getDevice();
    auto* rhiQueue = rhiDevice->getQueue(rhi::QueueType::Graphics);
    resourceManager = std::make_unique<ResourceManager>(rhiDevice, rhiQueue);
    sceneManager = std::make_unique<SceneManager>(rhiDevice, rhiQueue);

    // Create RHI resources
    createRHIDepthResources();
    createRHIUniformBuffers();
    createRHIBindGroups();

    // Phase 1.2: Initialize IBL (must be before building pipeline for bind group layout)
    createIBL();

    // HDR render target (all platforms — geometry renders here, post-process reads from here)
    createHDRRenderTarget();

    // Always create building pipeline for game world rendering
    createBuildingPipeline();

    // Phase 2.2: Create GPU frustum culling pipeline
    createCullingPipeline();

    // Phase 3.2: Async compute setup
    {
        const auto& features = rhiBridge->getDevice()->getCapabilities().getFeatures();
        if (features.dedicatedComputeQueue && features.timelineSemaphores) {
            computeTimelineSemaphore = rhiBridge->getDevice()->createTimelineSemaphore(0);
            if (computeTimelineSemaphore) {
                useAsyncCompute = true;
                LOG_INFO("Renderer") << "Async compute enabled (dedicated compute queue + timeline semaphores)";
            }
        }
        if (!useAsyncCompute) {
            LOG_INFO("Renderer") << "Async compute disabled, using inline compute on graphics queue";
        }
    }

    // Create particle renderer
    createParticleRenderer();

    // Phase 3.3: Create skybox renderer
    createSkyboxRenderer();

    // Phase 3.3: Create shadow renderer (CSM)
    createShadowRenderer();

#ifdef __EMSCRIPTEN__
    // Post-process pipelines must be created after HDR render target (bind groups reference texture views)
    createBloomPipelineWGSL();        // creates bloom textures + render pipelines
    createSSAOPipelineWGSL();         // creates SSAO textures + render pipelines
    createPostProcessPipelineWGSL();  // unified SSAO+Bloom+ACES+FXAA → swapchain
    printf("[DIAG] bloom=%s ssao=%s postprocess=%s\n",
        (wgslBloomPrefilterPipeline ? "OK" : "NULL"),
        (wgslSSAOPipeline           ? "OK" : "NULL"),
        (wgslPostprocessPipeline    ? "OK" : "NULL"));
#else
    // Vulkan: create compute resources first so postprocess bind group has all views
    createBloomPipeline();   // creates bloomTextureView
    createSSAOPipeline();    // creates ssaoBlurView
    createPostProcessPipeline(); // bind group binds bloom + ssao (both now available)
#endif

    // Phase 3: G-Buffer + Deferred Lighting (created after shadow + IBL are ready)
#ifdef __EMSCRIPTEN__
    // Step 6: WebGPU material bind-group infrastructure must exist BEFORE the
    // G-Buffer pipeline layout is built (it consumes the layout at set 2).
    createMaterialBindGroupInfrastructure();
#endif
    createGBufferPass();
    createDeferredLightingPass();

#ifndef __EMSCRIPTEN__
    // Phase 4: Bindless texture manager (Vulkan bindless — not available on WebGPU)
    createBindlessResources();

    // Phase 4.1: GPU Profiler (Vulkan-only)
    {
        auto* vulkanDevice = dynamic_cast<RHI::Vulkan::VulkanRHIDevice*>(rhiBridge->getDevice());
        if (vulkanDevice) {
            gpuProfiler = std::make_unique<GpuProfiler>(
                vulkanDevice->getVkDevice(),
                vulkanDevice->getVkPhysicalDevice(),
                MAX_FRAMES_IN_FLIGHT);
        }
    }
#else
    // P0.3: WebGPU GPU profiler via timestamp-query (per-pass real GPU ms).
    // No-op if adapter does not advertise the feature; CPU fallback still works.
    {
        auto* webgpuDevice = dynamic_cast<RHI::WebGPU::WebGPURHIDevice*>(rhiBridge->getDevice());
        if (webgpuDevice) {
            m_webgpuTimer = std::make_unique<WebGPUTimer>(
                webgpuDevice->getWGPUDevice(),
                webgpuDevice->getWGPUQueue(),
                MAX_FRAMES_IN_FLIGHT);
        }
    }
#endif

    // Phase 3.1: Log GPU memory statistics
    rhiBridge->getDevice()->logMemoryStats();
}

Renderer::~Renderer() {
    // Wait for device idle before destroying resources
    if (rhiBridge) {
        rhiBridge->waitIdle();
    }
    // All resources cleaned up by RAII in reverse declaration order
}

#ifdef __EMSCRIPTEN__
// Pass time getters — prefer GPU timing when supported, CPU as fallback.
bool Renderer::isGPUTimingAvailable() const {
    return m_webgpuTimer && m_webgpuTimer->isSupported();
}

float Renderer::getPassTimeGBuffer() const {
    if (isGPUTimingAvailable())
        return m_webgpuTimer->getElapsedMs(WebGPUTimer::TimerId::GBuffer);
    return m_passTimeGBuffer;
}
float Renderer::getPassTimeDeferred() const {
    if (isGPUTimingAvailable())
        return m_webgpuTimer->getElapsedMs(WebGPUTimer::TimerId::Deferred);
    return m_passTimeDeferred;
}
float Renderer::getPassTimeSSAO() const {
    if (isGPUTimingAvailable())
        return m_webgpuTimer->getElapsedMs(WebGPUTimer::TimerId::SSAO);
    return m_passTimeSSAO;
}
float Renderer::getPassTimeBloom() const {
    if (isGPUTimingAvailable())
        return m_webgpuTimer->getElapsedMs(WebGPUTimer::TimerId::Bloom);
    return m_passTimeBloom;
}
float Renderer::getPassTimePostProcess() const {
    if (isGPUTimingAvailable())
        return m_webgpuTimer->getElapsedMs(WebGPUTimer::TimerId::PostProcess);
    return m_passTimePostProcess;
}
float Renderer::getPassTimeTotal() const {
    if (isGPUTimingAvailable())
        return m_webgpuTimer->getElapsedMs(WebGPUTimer::TimerId::Frame);
    return m_passTimeTotal;
}
#endif

#ifndef __EMSCRIPTEN__
GpuProfiler* Renderer::getGpuProfiler() {
    return gpuProfiler.get();
}

Renderer::BindlessMetrics Renderer::getBindlessMetrics() const {
    BindlessMetrics m;
    m.lastInstanceCount = lastInstanceCount;

    // Bindless texture registry
    if (bindlessTextureManager) {
        m.bindlessAvailable  = bindlessTextureManager->isAvailable();
        m.registeredTextures = bindlessTextureManager->getRegisteredCount();
        m.maxTextures        = rendering::BindlessTextureManager::MAX_TEXTURES;
    }

    // VMA allocation stats
    if (auto* vkDev = dynamic_cast<const RHI::Vulkan::VulkanRHIDevice*>(rhiBridge->getDevice())) {
        VmaAllocator vma = const_cast<RHI::Vulkan::VulkanRHIDevice*>(vkDev)->getVmaAllocator();
        if (vma) {
            VmaTotalStatistics stats{};
            vmaCalculateStatistics(vma, &stats);
            m.vmaAllocCount     = stats.total.statistics.allocationCount;
            m.vmaAllocatedBytes = stats.total.statistics.allocationBytes;
            m.vmaReservedBytes  = stats.total.statistics.blockBytes;
        }
    }
    return m;
}
#endif

void Renderer::loadModel(const std::string& modelPath) {
    sceneManager->loadMesh(modelPath);  // Delegates to SceneManager

    // Phase 4.5: Create RHI buffers after loading mesh
    createRHIBuffers();
}

void Renderer::loadTexture(const std::string& texturePath) {
    resourceManager->loadTexture(texturePath);  // Delegates to ResourceManager
    // Descriptor updates handled via RHI bind groups
}

bool Renderer::setShowcaseMesh(const std::vector<Vertex>&         vertices,
                                const std::vector<uint32_t>&       indices,
                                const glm::mat4&                   worldMatrix,
                                const assets::ImportedMaterial*    material) {
    if (vertices.empty() || indices.empty()) {
        LOG_ERROR("Renderer") << "setShowcaseMesh: empty vertex/index data";
        return false;
    }
    if (!ssboBindGroupLayout) {
        LOG_ERROR("Renderer") << "setShowcaseMesh: SSBO layout not ready -- call after Renderer init";
        return false;
    }

    auto* device = rhiBridge->getDevice();
    auto* queue  = rhiBridge->getGraphicsQueue();

    // Drop any previously-installed showcase before creating new resources.
    clearShowcaseMesh();

    showcaseAsset.mesh = std::make_unique<Mesh>(device, queue, vertices, indices);
    if (!showcaseAsset.mesh->hasData()) {
        LOG_ERROR("Renderer") << "setShowcaseMesh: Mesh creation failed";
        clearShowcaseMesh();
        return false;
    }
    showcaseAsset.indexCount = static_cast<uint32_t>(showcaseAsset.mesh->getIndexCount());

    // Build the single ObjectData entry — matches the GPU layout used by
    // buildings (see src/rendering/InstancedRenderData.hpp). Bindless texture
    // index is set to the sentinel 0xFFFFFFFF so the shader falls back to the
    // scalar colorAndMetallic / roughnessAOPad inputs.
    glm::vec3 aabbMin( std::numeric_limits<float>::max());
    glm::vec3 aabbMax(-std::numeric_limits<float>::max());
    for (const auto& v : vertices) {
        aabbMin = glm::min(aabbMin, v.pos);
        aabbMax = glm::max(aabbMax, v.pos);
    }

    rendering::ObjectData od{};
    od.worldMatrix    = worldMatrix;
    od.boundingBoxMin = glm::vec4(aabbMin, 0.0f);
    od.boundingBoxMax = glm::vec4(aabbMax, 0.0f);
    if (material) {
        // glTF metallic-roughness factors. baseColorTexture is multiplied with
        // baseColorFactor at sample time; for textured assets the factor is
        // typically (1,1,1,1) so the texture color comes through unchanged.
        od.colorAndMetallic = glm::vec4(glm::vec3(material->baseColorFactor),
                                        material->metallicFactor);
        od.roughnessAOPad   = glm::vec4(material->roughnessFactor,
                                        1.0f,
                                        glm::uintBitsToFloat(0xFFFFFFFFu),
                                        0.0f);
    } else {
        // No material provided — neutral grey defaults (legacy showcase look).
        od.colorAndMetallic = glm::vec4(0.78f, 0.78f, 0.80f, 0.6f);
        od.roughnessAOPad   = glm::vec4(0.45f, 1.0f,
                                        glm::uintBitsToFloat(0xFFFFFFFFu),
                                        0.0f);
    }

    {
        rhi::BufferDesc bd;
        bd.size  = sizeof(rendering::ObjectData);
        bd.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::CopyDst;
        bd.label = "ShowcaseObjectData";
        showcaseAsset.objectBuffer = device->createBuffer(bd);
    }
    if (!showcaseAsset.objectBuffer) {
        LOG_ERROR("Renderer") << "setShowcaseMesh: failed to allocate ObjectData buffer";
        clearShowcaseMesh();
        return false;
    }
    showcaseAsset.objectBuffer->write(&od, sizeof(od));

    // visibleIndices buffer — set 1 binding 1 in the building shader. The
    // building pipeline expects a uint32 array; for the showcase we feed a
    // single zero so the shader reads ObjectData[0].
    {
        rhi::BufferDesc bd;
        bd.size  = sizeof(uint32_t);
        bd.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::CopyDst;
        bd.label = "ShowcaseVisibleIndices";
        showcaseAsset.visibleIndices = device->createBuffer(bd);
    }
    if (!showcaseAsset.visibleIndices) {
        LOG_ERROR("Renderer") << "setShowcaseMesh: failed to allocate visibleIndices buffer";
        clearShowcaseMesh();
        return false;
    }
    const uint32_t zero = 0u;
    showcaseAsset.visibleIndices->write(&zero, sizeof(zero));

    {
        rhi::BindGroupDesc bgDesc;
        bgDesc.layout = ssboBindGroupLayout.get();
        bgDesc.entries.push_back(rhi::BindGroupEntry::Buffer(0, showcaseAsset.objectBuffer.get()));
        bgDesc.entries.push_back(rhi::BindGroupEntry::Buffer(1, showcaseAsset.visibleIndices.get()));
        bgDesc.label = "ShowcaseSSBOBindGroup";
        showcaseAsset.ssboBindGroup = device->createBindGroup(bgDesc);
    }
    if (!showcaseAsset.ssboBindGroup) {
        LOG_ERROR("Renderer") << "setShowcaseMesh: failed to create SSBO bind group";
        clearShowcaseMesh();
        return false;
    }

    LOG_INFO("Renderer") << "Showcase mesh installed: "
                         << vertices.size() << " vertices, "
                         << indices.size()  << " indices";
    return true;
}

void Renderer::clearShowcaseMesh() {
#ifdef __EMSCRIPTEN__
    showcaseAsset.materialBindGroup.reset();
#endif
    showcaseAsset.baseColorView = nullptr;
    showcaseAsset.normalView    = nullptr;
    showcaseAsset.mrView        = nullptr;
    showcaseAsset.emissiveView  = nullptr;
    showcaseAsset.aoView        = nullptr;
    showcaseAsset.materialTextureViews.clear();
    showcaseAsset.materialTextures.clear();
    showcaseAsset.ssboBindGroup.reset();
    showcaseAsset.visibleIndices.reset();
    showcaseAsset.objectBuffer.reset();
    showcaseAsset.mesh.reset();
    showcaseAsset.indexCount = 0;
}

size_t Renderer::uploadShowcaseMaterialTextures(const assets::ImportedAsset& asset,
                                                 uint32_t materialIndex) {
    if (materialIndex >= asset.materials.size()) {
        LOG_ERROR("Renderer") << "uploadShowcaseMaterialTextures: materialIndex out of range";
        return 0;
    }
    if (!resourceManager) {
        LOG_ERROR("Renderer") << "uploadShowcaseMaterialTextures: ResourceManager not ready";
        return 0;
    }

    const auto& mat = asset.materials[materialIndex];

    // Tag each texture slot in the source asset with the color space its
    // material binding implies. Linear is the default (data textures); only
    // baseColor and emissive get sRGB. Iterating per-material is enough for
    // the showcase: a single material owns at most one of each slot.
    std::vector<rhi::TextureFormat> formatByIndex(
        asset.textures.size(), rhi::TextureFormat::RGBA8Unorm);

    auto tagSrgb = [&](uint32_t idx) {
        if (idx < formatByIndex.size()) formatByIndex[idx] = rhi::TextureFormat::RGBA8UnormSrgb;
    };
    tagSrgb(mat.baseColorTextureIndex);
    tagSrgb(mat.emissiveTextureIndex);
    // normal / metallicRoughness / occlusion stay linear (default above).

    showcaseAsset.materialTextures.clear();
    showcaseAsset.materialTextures.resize(asset.textures.size());
    showcaseAsset.materialTextureViews.clear();
    showcaseAsset.materialTextureViews.resize(asset.textures.size());

    size_t uploaded = 0;
    uint64_t bytesUploaded = 0;
    for (uint32_t i = 0; i < asset.textures.size(); ++i) {
        const auto& src = asset.textures[i];
        if (src.pixelsRGBA8.empty() || src.width == 0 || src.height == 0) {
            // Texture decode failed earlier in AssetImporter; leave slot null.
            continue;
        }
        auto tex = resourceManager->uploadRGBA8FromMemory(
            src.pixelsRGBA8.data(), src.width, src.height, formatByIndex[i]);
        if (!tex) {
            LOG_ERROR("Renderer") << "uploadShowcaseMaterialTextures: upload failed for texture " << i;
            continue;
        }

        // Create a view for this texture so bind groups can reference it.
        rhi::TextureViewDesc vd;
        vd.dimension = rhi::TextureViewDimension::View2D;
        vd.format    = formatByIndex[i];
        auto view = tex->createView(vd);

        showcaseAsset.materialTextures[i]     = std::move(tex);
        showcaseAsset.materialTextureViews[i] = std::move(view);
        ++uploaded;
        bytesUploaded += static_cast<uint64_t>(src.width) * src.height * 4u;
    }

    // Resolve per-slot view pointers using the material's texture indices.
    auto resolveView = [&](uint32_t idx) -> rhi::RHITextureView* {
        if (idx < showcaseAsset.materialTextureViews.size()
            && showcaseAsset.materialTextureViews[idx]) {
            return showcaseAsset.materialTextureViews[idx].get();
        }
        return nullptr;
    };
    showcaseAsset.baseColorView = resolveView(mat.baseColorTextureIndex);
    showcaseAsset.normalView    = resolveView(mat.normalTextureIndex);
    showcaseAsset.mrView        = resolveView(mat.metallicRoughnessTextureIndex);
    showcaseAsset.emissiveView  = resolveView(mat.emissiveTextureIndex);
    showcaseAsset.aoView        = resolveView(mat.occlusionTextureIndex);

#ifdef __EMSCRIPTEN__
    // Build the WebGPU set 2 bind group: each slot picks the helmet's own
    // view if available, otherwise the Renderer's default dummy view. This
    // means the shader can always sample unconditionally.
    if (materialBindGroupLayout && defaultBaseColorView && defaultNormalView
        && defaultMRView && defaultEmissiveView && defaultAOView && materialSampler) {
        rhi::RHITextureView* bcV = showcaseAsset.baseColorView ? showcaseAsset.baseColorView : defaultBaseColorView.get();
        rhi::RHITextureView* nV  = showcaseAsset.normalView    ? showcaseAsset.normalView    : defaultNormalView.get();
        rhi::RHITextureView* mV  = showcaseAsset.mrView        ? showcaseAsset.mrView        : defaultMRView.get();
        rhi::RHITextureView* eV  = showcaseAsset.emissiveView  ? showcaseAsset.emissiveView  : defaultEmissiveView.get();
        rhi::RHITextureView* aV  = showcaseAsset.aoView        ? showcaseAsset.aoView        : defaultAOView.get();

        rhi::BindGroupDesc bgDesc;
        bgDesc.layout = materialBindGroupLayout.get();
        bgDesc.label  = "ShowcaseMaterialBindGroup";
        bgDesc.entries.push_back(rhi::BindGroupEntry::TextureView(0, bcV));
        bgDesc.entries.push_back(rhi::BindGroupEntry::TextureView(1, nV));
        bgDesc.entries.push_back(rhi::BindGroupEntry::TextureView(2, mV));
        bgDesc.entries.push_back(rhi::BindGroupEntry::TextureView(3, eV));
        bgDesc.entries.push_back(rhi::BindGroupEntry::TextureView(4, aV));
        bgDesc.entries.push_back(rhi::BindGroupEntry::Sampler    (5, materialSampler.get()));
        showcaseAsset.materialBindGroup = rhiBridge->getDevice()->createBindGroup(bgDesc);
        if (!showcaseAsset.materialBindGroup) {
            LOG_ERROR("Renderer") << "Failed to create showcase material bind group";
        }
    }
#endif

    LOG_INFO("Renderer") << "Uploaded " << uploaded << "/" << asset.textures.size()
                         << " showcase textures (" << (bytesUploaded / 1024u)
                         << " KiB total) for material " << materialIndex;

    // Diagnostic: report which material slots resolved to real glTF textures
    // vs the engine default dummies. If a slot says "dummy" but the source
    // material was supposed to provide it, that points at AssetImporter or
    // texture-index resolution rather than the upload itself.
    auto slotTag = [](const rhi::RHITextureView* v) { return v ? "real" : "dummy"; };
    LOG_INFO("Renderer") << "Showcase material slot resolution: "
                         << "baseColor=" << slotTag(showcaseAsset.baseColorView)
                         << " normal="    << slotTag(showcaseAsset.normalView)
                         << " mr="        << slotTag(showcaseAsset.mrView)
                         << " emissive="  << slotTag(showcaseAsset.emissiveView)
                         << " ao="        << slotTag(showcaseAsset.aoView);
    return uploaded;
}

void Renderer::waitIdle() {
    rhiBridge->waitIdle();
}

void Renderer::handleFramebufferResize() {
    recreateSwapchain();
}

void Renderer::handleFramebufferResize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    rhiBridge->waitIdle();
    rhiBridge->createSwapchain(static_cast<uint32_t>(width), static_cast<uint32_t>(height), true);
    createRHIDepthResources();
    createHDRRenderTarget();

#ifdef __EMSCRIPTEN__
    if (gBufferPass && gBufferPass->isInitialized() && rhiDepthImageView) {
        gBufferPass->resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                            rhiDepthImageView.get());
        createDeferredLightingPass();
    }
    createBloomPipelineWGSL();
    createSSAOPipelineWGSL();

    if (wgslPostprocessLayout && hdrColorView && hdrSampler) {
        rhi::RHITextureView* bv   = bloomTextureView ? bloomTextureView.get() : hdrColorView.get();
        rhi::RHITextureView* ssao = ssaoBlurView     ? ssaoBlurView.get()     : hdrColorView.get();
        rhi::BindGroupDesc bd;
        bd.layout = wgslPostprocessLayout.get();
        bd.entries = {
            rhi::BindGroupEntry::TextureView(0, hdrColorView.get()),
            rhi::BindGroupEntry::TextureView(1, bv),
            rhi::BindGroupEntry::TextureView(2, ssao),
            rhi::BindGroupEntry::Sampler    (3, hdrSampler.get()),
            rhi::BindGroupEntry::Buffer     (4, wgslPostprocessParamsUBO.get(), 0, 48),
        };
        bd.label = "PostProcess Bind Group";
        wgslPostprocessBG = rhiBridge->getDevice()->createBindGroup(bd);
    }
#else
    recreatePostProcessResources();
#endif
}

void Renderer::updateCamera(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& position) {
    viewMatrix = view;
    projectionMatrix = projection;
    cameraPosition = position;
}

void Renderer::submitInstancedRenderData(const rendering::InstancedRenderData& data) {
    // Store copy of data for this frame (fixes dangling pointer issue)
    pendingInstancedData = data;
}

void Renderer::submitParticleSystem(effects::ParticleSystem* particleSystem) {
    pendingParticleSystem = particleSystem;
}

glm::vec3 Renderer::getMeshCenter() const {
    auto* mesh = sceneManager->getPrimaryMesh();
    if (mesh) {
        return mesh->getBoundingBoxCenter();
    }
    return glm::vec3(0.0f, 0.0f, 0.0f);
}

float Renderer::getMeshRadius() const {
    auto* mesh = sceneManager->getPrimaryMesh();
    if (mesh) {
        return mesh->getBoundingBoxRadius();
    }
    return 0.0f;
}

// Phase 8: All legacy resource creation methods removed - using only RHI

void Renderer::recreateSwapchain() {
    // Wait for window to be visible
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }

    rhiBridge->waitIdle();

    // Recreate RHI swapchain and depth resources
    rhiBridge->createSwapchain(static_cast<uint32_t>(width), static_cast<uint32_t>(height), true);
    createRHIDepthResources();
    createRHIPipeline();  // Pipeline needs recreation with new render pass
    createHDRRenderTarget();

#ifdef __EMSCRIPTEN__
    if (gBufferPass && gBufferPass->isInitialized() && rhiDepthImageView) {
        gBufferPass->resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                            rhiDepthImageView.get());
        createDeferredLightingPass();
    }
    // Bloom + SSAO textures are resolution-dependent — recreate at new half-res size
    createBloomPipelineWGSL();
    createSSAOPipelineWGSL();

    if (wgslPostprocessLayout && hdrColorView && hdrSampler) {
        rhi::RHITextureView* bv   = bloomTextureView ? bloomTextureView.get() : hdrColorView.get();
        rhi::RHITextureView* ssao = ssaoBlurView     ? ssaoBlurView.get()     : hdrColorView.get();
        rhi::BindGroupDesc bd;
        bd.layout = wgslPostprocessLayout.get();
        bd.entries = {
            rhi::BindGroupEntry::TextureView(0, hdrColorView.get()),
            rhi::BindGroupEntry::TextureView(1, bv),
            rhi::BindGroupEntry::TextureView(2, ssao),
            rhi::BindGroupEntry::Sampler    (3, hdrSampler.get()),
            rhi::BindGroupEntry::Buffer     (4, wgslPostprocessParamsUBO.get(), 0, 48),
        };
        bd.label = "PostProcess Bind Group";
        wgslPostprocessBG = rhiBridge->getDevice()->createBindGroup(bd);
    }
#else
    recreatePostProcessResources();
#endif

    // Notify ImGui of resize
#ifndef __EMSCRIPTEN__
    if (imguiManager) {
        imguiManager->handleResize();
    }
#endif
}

void Renderer::initImGui(GLFWwindow* window) {
#ifndef __EMSCRIPTEN__
    // Phase 6: Create ImGui manager using RHI types
    auto* rhiDevice = rhiBridge->getDevice();
    auto* rhiSwapchain = rhiBridge->getSwapchain();

    if (rhiDevice && rhiSwapchain) {
        imguiManager = std::make_unique<ImGuiManager>(window, rhiDevice, rhiSwapchain);
    }
#else
    (void)window;  // Suppress unused parameter warning
#endif
}

// ============================================================================
// Phase 4: RHI Resource Creation (parallel to legacy resources)
// ============================================================================

void Renderer::createRHIDepthResources() {
    if (!rhiBridge || !rhiBridge->isReady()) {
        return;
    }

    auto* rhiDevice = rhiBridge->getDevice();
    auto* rhiSwapchain = rhiBridge->getSwapchain();
    if (!rhiSwapchain) {
        return;  // Swapchain not created yet
    }

    // Create depth texture using RHI
    rhi::TextureDesc depthDesc;
    depthDesc.size = rhi::Extent3D(rhiSwapchain->getWidth(), rhiSwapchain->getHeight(), 1);
    depthDesc.format = rhi::TextureFormat::Depth32Float;
    depthDesc.usage = rhi::TextureUsage::DepthStencil | rhi::TextureUsage::Sampled;
    depthDesc.transient = false;  // Must be readable by SSAO compute shader
    depthDesc.label = "RHI Depth Image";

    rhiDepthImage = rhiDevice->createTexture(depthDesc);

    // Create cached depth image view
    if (rhiDepthImage) {
        rhi::TextureViewDesc viewDesc;
        viewDesc.format = rhi::TextureFormat::Depth32Float;
        viewDesc.dimension = rhi::TextureViewDimension::View2D;
        rhiDepthImageView = rhiDepthImage->createView(viewDesc);
    }
}

void Renderer::createRHIUniformBuffers() {
    if (!rhiBridge || !rhiBridge->isReady()) {
        return;
    }

    auto* rhiDevice = rhiBridge->getDevice();
    rhiUniformBuffers.clear();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        rhi::BufferDesc bufferDesc;
        bufferDesc.size = sizeof(UniformBufferObject);  // model + view + proj
        bufferDesc.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::MapWrite;
        bufferDesc.mappedAtCreation = false;  // Use write() for updates, not mapping
        bufferDesc.label = "RHI Uniform Buffer";

        rhiUniformBuffers.push_back(rhiDevice->createBuffer(bufferDesc));
    }
}

void Renderer::createRHIBindGroups() {
    if (!rhiBridge || !rhiBridge->isReady() || rhiUniformBuffers.empty()) {
        return;
    }

    auto* rhiDevice = rhiBridge->getDevice();

    // Create bind group layout
    rhi::BindGroupLayoutDesc layoutDesc;

    // Binding 0: Uniform buffer
    rhi::BindGroupLayoutEntry uboEntry;
    uboEntry.binding = 0;
    uboEntry.visibility = rhi::ShaderStage::Vertex;
    uboEntry.type = rhi::BindingType::UniformBuffer;
    layoutDesc.entries.push_back(uboEntry);

#ifndef __EMSCRIPTEN__
    // Binding 1: Combined image sampler (Vulkan legacy path only — not used on WebGPU)
    rhi::BindGroupLayoutEntry samplerEntry;
    samplerEntry.binding = 1;
    samplerEntry.visibility = rhi::ShaderStage::Fragment;
    samplerEntry.type = rhi::BindingType::SampledTexture;
    layoutDesc.entries.push_back(samplerEntry);
#endif

    layoutDesc.label = "RHI Main Bind Group Layout";
    rhiBindGroupLayout = rhiDevice->createBindGroupLayout(layoutDesc);

    // Create bind groups for each frame
    rhiBindGroups.clear();
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        rhi::BindGroupDesc bindGroupDesc;
        bindGroupDesc.layout = rhiBindGroupLayout.get();

        // Add uniform buffer entry
        bindGroupDesc.entries.push_back(
            rhi::BindGroupEntry::Buffer(0, rhiUniformBuffers[i].get())
        );

        bindGroupDesc.label = "RHI Main Bind Group";
        rhiBindGroups.push_back(rhiDevice->createBindGroup(bindGroupDesc));
    }
}

// ============================================================================
// Phase 4.4: RHI Pipeline Creation
// ============================================================================

void Renderer::createRHIPipeline() {
    if (!rhiBridge || !rhiBridge->isReady() || !rhiBindGroupLayout) {
        return;
    }

    // Phase 8: Ensure swapchain is created before pipeline (needed for render pass on Linux)
    if (!rhiBridge->getSwapchain()) {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        rhiBridge->createSwapchain(static_cast<uint32_t>(width), static_cast<uint32_t>(height), true);
    }

    // Select shader path
    std::string shaderPath = "shaders/slang.spv";

    // Create vertex shader
    rhiVertexShader = rhiBridge->createShaderFromFile(
        shaderPath,
        rhi::ShaderStage::Vertex,
        "vertMain"
    );

    // Create fragment shader
    rhiFragmentShader = rhiBridge->createShaderFromFile(
        shaderPath,
        rhi::ShaderStage::Fragment,
        "fragMain"
    );

    if (!rhiVertexShader || !rhiFragmentShader) {
        LOG_ERROR("Renderer") << "Failed to create RHI shaders";
        return;
    }

    // Create pipeline layout
    rhi::PipelineLayoutDesc layoutDesc;
    layoutDesc.bindGroupLayouts.push_back(rhiBindGroupLayout.get());
    rhiPipelineLayout = rhiBridge->createPipelineLayout(layoutDesc);

    if (!rhiPipelineLayout) {
        LOG_ERROR("Renderer") << "Failed to create RHI pipeline layout";
        return;
    }

    // Setup vertex state - matches Vertex struct (pos / normal / texCoord / tangent)
    rhi::VertexBufferLayout vertexLayout;
    vertexLayout.stride = sizeof(Vertex);
    vertexLayout.inputRate = rhi::VertexInputRate::Vertex;
    vertexLayout.attributes = {
        rhi::VertexAttribute(0, 0, rhi::TextureFormat::RGB32Float, offsetof(Vertex, pos)),     // position
        rhi::VertexAttribute(1, 0, rhi::TextureFormat::RGB32Float, offsetof(Vertex, normal)),  // normal
        rhi::VertexAttribute(2, 0, rhi::TextureFormat::RG32Float,  offsetof(Vertex, texCoord)),// texCoord
        rhi::VertexAttribute(3, 0, rhi::TextureFormat::RGB32Float, offsetof(Vertex, tangent))  // tangent
    };

    // Create render pipeline descriptor
    rhi::RenderPipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = rhiVertexShader.get();
    pipelineDesc.fragmentShader = rhiFragmentShader.get();
    pipelineDesc.layout = rhiPipelineLayout.get();
    pipelineDesc.vertex.buffers.push_back(vertexLayout);

    // Primitive state
    pipelineDesc.primitive.topology = rhi::PrimitiveTopology::TriangleList;
    pipelineDesc.primitive.cullMode = rhi::CullMode::Back;
    pipelineDesc.primitive.frontFace = rhi::FrontFace::Clockwise;  // Cube mesh uses CW winding

    // Depth-stencil state
    rhi::DepthStencilState depthStencilState;
    depthStencilState.depthTestEnabled = true;
    depthStencilState.depthWriteEnabled = true;
    depthStencilState.depthCompare = rhi::CompareOp::Less;
    depthStencilState.format = rhi::TextureFormat::Depth32Float;
    pipelineDesc.depthStencil = &depthStencilState;

    // Color target: RGBA16Float (geometry renders to HDR offscreen target on all platforms)
    rhi::ColorTargetState colorTarget;
    auto* swapchain = rhiBridge->getSwapchain();
    colorTarget.format = rhi::TextureFormat::RGBA16Float;
    colorTarget.blend.blendEnabled = false;
    pipelineDesc.colorTargets.push_back(colorTarget);

    pipelineDesc.label = "RHI Main Pipeline";

    if (swapchain) {
        swapchain->ensureRenderResourcesReady(rhiDepthImageView.get());

#ifdef __linux__
        // Linux: use HDR render pass (RGBA16Float + depth), matching the geometry render pass
        auto* vulkanSwapchain = dynamic_cast<RHI::Vulkan::VulkanRHISwapchain*>(swapchain);
        if (vulkanSwapchain) {
            VkRenderPass vkPass = static_cast<VkRenderPass>(vulkanSwapchain->getHDRRenderPass());
            pipelineDesc.nativeRenderPass = reinterpret_cast<void*>(vkPass);
        }
#endif
    }

    // Create pipeline
    rhiPipeline = rhiBridge->createRenderPipeline(pipelineDesc);

    if (rhiPipeline) {
        LOG_INFO("Renderer") << "RHI Pipeline created successfully";
    } else {
        LOG_ERROR("Renderer") << "Failed to create RHI pipeline";
    }
}

// ============================================================================
// Phase 4.5: RHI Vertex/Index Buffer Creation
// ============================================================================

void Renderer::createRHIBuffers() {
    if (!rhiBridge || !rhiBridge->isReady() || !sceneManager) {
        return;
    }

    auto* mesh = sceneManager->getPrimaryMesh();
    if (!mesh || !mesh->hasData()) {
        return;
    }

    auto* rhiDevice = rhiBridge->getDevice();

    // Get raw vertex and index data from mesh
    const auto& vertices = mesh->getVertices();
    const auto& indices = mesh->getIndices();

    size_t vertexCount = vertices.size();
    size_t indexCount = indices.size();
    size_t vertexBufferSize = vertexCount * sizeof(Vertex);
    size_t indexBufferSize = indexCount * sizeof(uint32_t);

    // Create vertex staging buffer (host-visible, mapped at creation)
    rhi::BufferDesc vertexStagingDesc;
    vertexStagingDesc.size = vertexBufferSize;
    vertexStagingDesc.usage = rhi::BufferUsage::CopySrc | rhi::BufferUsage::MapWrite;
    vertexStagingDesc.mappedAtCreation = true;
    vertexStagingDesc.label = "RHI Vertex Staging Buffer";

    auto vertexStagingBuffer = rhiDevice->createBuffer(vertexStagingDesc);

    // Copy vertex data to staging buffer
    if (vertexStagingBuffer) {
        void* mappedData = vertexStagingBuffer->getMappedData();
        if (mappedData) {
            memcpy(mappedData, vertices.data(), vertexBufferSize);
            vertexStagingBuffer->unmap();
        }
    }

    // Create device-local vertex buffer
    rhi::BufferDesc vertexBufferDesc;
    vertexBufferDesc.size = vertexBufferSize;
    vertexBufferDesc.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::CopyDst;
    vertexBufferDesc.mappedAtCreation = false;
    vertexBufferDesc.label = "RHI Vertex Buffer";

    rhiVertexBuffer = rhiDevice->createBuffer(vertexBufferDesc);

    // Create index staging buffer
    rhi::BufferDesc indexStagingDesc;
    indexStagingDesc.size = indexBufferSize;
    indexStagingDesc.usage = rhi::BufferUsage::CopySrc | rhi::BufferUsage::MapWrite;
    indexStagingDesc.mappedAtCreation = true;
    indexStagingDesc.label = "RHI Index Staging Buffer";

    auto indexStagingBuffer = rhiDevice->createBuffer(indexStagingDesc);

    // Copy index data to staging buffer
    if (indexStagingBuffer) {
        void* mappedData = indexStagingBuffer->getMappedData();
        if (mappedData) {
            memcpy(mappedData, indices.data(), indexBufferSize);
            indexStagingBuffer->unmap();
        }
    }

    // Create device-local index buffer
    rhi::BufferDesc indexBufferDesc;
    indexBufferDesc.size = indexBufferSize;
    indexBufferDesc.usage = rhi::BufferUsage::Index | rhi::BufferUsage::CopyDst;
    indexBufferDesc.mappedAtCreation = false;
    indexBufferDesc.label = "RHI Index Buffer";

    rhiIndexBuffer = rhiDevice->createBuffer(indexBufferDesc);
    rhiIndexCount = static_cast<uint32_t>(indexCount);

    // Copy data from staging to device-local buffers using command buffer
    if (rhiVertexBuffer && rhiIndexBuffer && vertexStagingBuffer && indexStagingBuffer) {
        auto encoder = rhiDevice->createCommandEncoder();
        if (encoder) {
            encoder->copyBufferToBuffer(
                vertexStagingBuffer.get(), 0,
                rhiVertexBuffer.get(), 0,
                vertexBufferSize
            );
            encoder->copyBufferToBuffer(
                indexStagingBuffer.get(), 0,
                rhiIndexBuffer.get(), 0,
                indexBufferSize
            );

            auto commandBuffer = encoder->finish();
            if (commandBuffer) {
                // Submit and wait for completion
                auto* queue = rhiDevice->getQueue(rhi::QueueType::Graphics);
                auto fence = rhiDevice->createFence(false);
                queue->submit(commandBuffer.get(), fence.get());
                fence->wait();

                // Phase 7.5: Wait for device idle to ensure command buffer is fully retired
                // before it's destroyed (prevents "command buffer in use" error)
                rhiDevice->waitIdle();
            }
        }

        LOG_INFO("Renderer") << "RHI buffers uploaded: " 
                  << vertexCount << " vertices (" << vertexBufferSize << " bytes), " 
                  << indexCount << " indices (" << indexBufferSize << " bytes)";
    }
}

// ============================================================================
// Building Instancing Pipeline Creation
// ============================================================================

void Renderer::createBuildingPipeline() {
    if (!rhiBridge || !rhiBridge->isReady() || !rhiBindGroupLayout) {
        return;
    }

    // Ensure swapchain is created
    if (!rhiBridge->getSwapchain()) {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        rhiBridge->createSwapchain(static_cast<uint32_t>(width), static_cast<uint32_t>(height), true);
    }

    // Create building shaders
#ifdef __EMSCRIPTEN__
    // WebGPU/Emscripten: Load WGSL shader
    auto wgslCodeRaw = FileUtils::readFile("shaders/building.wgsl");
    if (!wgslCodeRaw.empty()) {
        std::vector<uint8_t> wgslCode(wgslCodeRaw.begin(), wgslCodeRaw.end());

        rhi::ShaderSource vertSource(rhi::ShaderLanguage::WGSL, wgslCode, rhi::ShaderStage::Vertex, "vs_main");
        rhi::ShaderDesc vertDesc(vertSource, "BuildingVertexShader");
        buildingVertexShader = rhiBridge->getDevice()->createShader(vertDesc);

        rhi::ShaderSource fragSource(rhi::ShaderLanguage::WGSL, wgslCode, rhi::ShaderStage::Fragment, "fs_main");
        rhi::ShaderDesc fragDesc(fragSource, "BuildingFragmentShader");
        buildingFragmentShader = rhiBridge->getDevice()->createShader(fragDesc);
    }
    LOG_DEBUG("Renderer") << "Using building shaders (WGSL)";
#else
    // Vulkan/Native: Load SPIR-V shaders
    buildingVertexShader = rhiBridge->createShaderFromFile(
        "shaders/building.vert.spv",
        rhi::ShaderStage::Vertex,
        "main"
    );

    buildingFragmentShader = rhiBridge->createShaderFromFile(
        "shaders/building.frag.spv",
        rhi::ShaderStage::Fragment,
        "main"
    );
    LOG_DEBUG("Renderer") << "Using building shaders (SPIR-V)";
#endif

    if (!buildingVertexShader || !buildingFragmentShader) {
        LOG_ERROR("Renderer") << "Failed to create building shaders";
        return;
    }

    // Create dedicated bind group layout for buildings (UBO + shadow map)
    rhi::BindGroupLayoutDesc buildingLayoutDesc;

    // Binding 0: Uniform buffer (vertex + fragment)
    rhi::BindGroupLayoutEntry uboEntry;
    uboEntry.binding = 0;
    uboEntry.visibility = rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment;
    uboEntry.type = rhi::BindingType::UniformBuffer;
    buildingLayoutDesc.entries.push_back(uboEntry);

    // Binding 1: Shadow map texture (CSM 2D array, fragment only)
    rhi::BindGroupLayoutEntry shadowTexEntry;
    shadowTexEntry.binding = 1;
    shadowTexEntry.visibility = rhi::ShaderStage::Fragment;
    shadowTexEntry.type = rhi::BindingType::DepthTexture;
    shadowTexEntry.textureViewDimension = rhi::TextureViewDimension::View2DArray;
    buildingLayoutDesc.entries.push_back(shadowTexEntry);

    // Binding 2: Shadow sampler (fragment only) - non-filtering for depth texture
    rhi::BindGroupLayoutEntry shadowSamplerEntry;
    shadowSamplerEntry.binding = 2;
    shadowSamplerEntry.visibility = rhi::ShaderStage::Fragment;
    shadowSamplerEntry.type = rhi::BindingType::NonFilteringSampler;
    buildingLayoutDesc.entries.push_back(shadowSamplerEntry);

    // Binding 3: IBL Irradiance cubemap (fragment only)
    rhi::BindGroupLayoutEntry irrEntry;
    irrEntry.binding = 3;
    irrEntry.visibility = rhi::ShaderStage::Fragment;
    irrEntry.type = rhi::BindingType::SampledTexture;
    irrEntry.textureViewDimension = rhi::TextureViewDimension::ViewCube;
    buildingLayoutDesc.entries.push_back(irrEntry);

    // Binding 4: IBL Prefiltered env cubemap (fragment only)
    rhi::BindGroupLayoutEntry prefiltEntry;
    prefiltEntry.binding = 4;
    prefiltEntry.visibility = rhi::ShaderStage::Fragment;
    prefiltEntry.type = rhi::BindingType::SampledTexture;
    prefiltEntry.textureViewDimension = rhi::TextureViewDimension::ViewCube;
    buildingLayoutDesc.entries.push_back(prefiltEntry);

    // Binding 5: IBL BRDF LUT (fragment only)
    rhi::BindGroupLayoutEntry brdfEntry;
    brdfEntry.binding = 5;
    brdfEntry.visibility = rhi::ShaderStage::Fragment;
    brdfEntry.type = rhi::BindingType::SampledTexture;
    buildingLayoutDesc.entries.push_back(brdfEntry);

    // Binding 6: IBL sampler (fragment only)
    rhi::BindGroupLayoutEntry iblSamplerEntry;
    iblSamplerEntry.binding = 6;
    iblSamplerEntry.visibility = rhi::ShaderStage::Fragment;
    iblSamplerEntry.type = rhi::BindingType::Sampler;
    buildingLayoutDesc.entries.push_back(iblSamplerEntry);

    buildingLayoutDesc.label = "Building Bind Group Layout";

    buildingBindGroupLayout = rhiBridge->getDevice()->createBindGroupLayout(buildingLayoutDesc);

    if (!buildingBindGroupLayout) {
        LOG_ERROR("Renderer") << "Failed to create building bind group layout";
        return;
    }

    // Note: Bind groups will be created/updated in createShadowRenderer() after shadow renderer is ready
    buildingBindGroups.clear();
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        buildingBindGroups.push_back(nullptr);
    }

    // Phase 2.1+2.2: Create SSBO bind group layout (set 1) for per-object data + visible indices
    {
        rhi::BindGroupLayoutDesc ssboLayoutDesc;

        rhi::BindGroupLayoutEntry ssboEntry;
        ssboEntry.binding = 0;
        ssboEntry.visibility = rhi::ShaderStage::Vertex;
        ssboEntry.type = rhi::BindingType::ReadOnlyStorageBuffer;  // shader: var<storage, read>
        ssboLayoutDesc.entries.push_back(ssboEntry);

        // Phase 2.2: Visible indices buffer for frustum culling indirection
        rhi::BindGroupLayoutEntry visibleIndicesEntry;
        visibleIndicesEntry.binding = 1;
        visibleIndicesEntry.visibility = rhi::ShaderStage::Vertex;
        visibleIndicesEntry.type = rhi::BindingType::ReadOnlyStorageBuffer;  // shader: var<storage, read>
        ssboLayoutDesc.entries.push_back(visibleIndicesEntry);

        ssboLayoutDesc.label = "SSBO Bind Group Layout";
        ssboBindGroupLayout = rhiBridge->getDevice()->createBindGroupLayout(ssboLayoutDesc);

        if (!ssboBindGroupLayout) {
            LOG_ERROR("Renderer") << "Failed to create SSBO bind group layout";
            return;
        }
    }

    // Create pipeline layout with two bind group layouts: set 0 (UBO+textures), set 1 (SSBO)
    rhi::PipelineLayoutDesc layoutDesc;
    layoutDesc.bindGroupLayouts.push_back(buildingBindGroupLayout.get());
    layoutDesc.bindGroupLayouts.push_back(ssboBindGroupLayout.get());
    buildingPipelineLayout = rhiBridge->createPipelineLayout(layoutDesc);

    if (!buildingPipelineLayout) {
        LOG_ERROR("Renderer") << "Failed to create building pipeline layout";
        return;
    }

    // Setup vertex state - per-vertex attributes only (binding 0)
    // Phase 2.1: Instance data now comes from SSBO, not vertex attributes
    rhi::VertexBufferLayout vertexLayout;
    vertexLayout.stride = sizeof(Vertex);
    vertexLayout.inputRate = rhi::VertexInputRate::Vertex;
    vertexLayout.attributes = {
        rhi::VertexAttribute(0, 0, rhi::TextureFormat::RGB32Float, offsetof(Vertex, pos)),     // inPosition
        rhi::VertexAttribute(1, 0, rhi::TextureFormat::RGB32Float, offsetof(Vertex, normal)),  // inNormal
        rhi::VertexAttribute(2, 0, rhi::TextureFormat::RG32Float,  offsetof(Vertex, texCoord)),// inTexCoord
        rhi::VertexAttribute(3, 0, rhi::TextureFormat::RGB32Float, offsetof(Vertex, tangent))  // inTangent
    };

    // Create render pipeline descriptor
    rhi::RenderPipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = buildingVertexShader.get();
    pipelineDesc.fragmentShader = buildingFragmentShader.get();
    pipelineDesc.layout = buildingPipelineLayout.get();
    pipelineDesc.vertex.buffers.push_back(vertexLayout);

    // Primitive state
    pipelineDesc.primitive.topology = rhi::PrimitiveTopology::TriangleList;
    pipelineDesc.primitive.cullMode = rhi::CullMode::Back;
    pipelineDesc.primitive.frontFace = rhi::FrontFace::Clockwise;  // Cube mesh uses CW winding

    // Depth-stencil state
    rhi::DepthStencilState depthStencilState;
    depthStencilState.depthTestEnabled = true;
    depthStencilState.depthWriteEnabled = true;
    depthStencilState.depthCompare = rhi::CompareOp::Less;
    depthStencilState.format = rhi::TextureFormat::Depth32Float;
    pipelineDesc.depthStencil = &depthStencilState;

    // Color target format: RGBA16Float (geometry renders to HDR offscreen target on all platforms)
    auto* swapchain = rhiBridge->getSwapchain();
    rhi::ColorTargetState colorTarget;
    colorTarget.format         = rhi::TextureFormat::RGBA16Float;
    colorTarget.blend.blendEnabled = false;
    pipelineDesc.colorTargets.push_back(colorTarget);

    pipelineDesc.label = "Building Instancing Pipeline";

    // Ensure platform-specific render resources are ready
    if (swapchain) {
        swapchain->ensureRenderResourcesReady(rhiDepthImageView.get());

#ifdef __linux__
        // Linux: use the HDR render pass (RGBA16Float + depth)
        auto* vulkanSwapchain = dynamic_cast<RHI::Vulkan::VulkanRHISwapchain*>(swapchain);
        if (vulkanSwapchain) {
            VkRenderPass vkPass = static_cast<VkRenderPass>(vulkanSwapchain->getHDRRenderPass());
            pipelineDesc.nativeRenderPass = reinterpret_cast<void*>(vkPass);
        }
#endif
    }

    // Create pipeline
    buildingPipeline = rhiBridge->createRenderPipeline(pipelineDesc);

    if (buildingPipeline) {
        LOG_INFO("Renderer") << "Building instancing pipeline created successfully";
    } else {
        LOG_ERROR("Renderer") << "Failed to create building pipeline";
    }
}

// ============================================================================
// Phase 3.1: Particle Renderer Creation
// ============================================================================

void Renderer::createParticleRenderer() {
    if (!rhiBridge || !rhiBridge->isReady()) {
        return;
    }

    auto* rhiDevice = rhiBridge->getDevice();
    auto* rhiQueue = rhiBridge->getGraphicsQueue();
    auto* swapchain = rhiBridge->getSwapchain();

    if (!rhiDevice || !rhiQueue || !swapchain) {
        return;
    }

    // Create particle renderer
    particleRenderer = std::make_unique<effects::ParticleRenderer>(rhiDevice, rhiQueue);

    // Initialize with color format and depth format
    // All platforms: renders into HDR RGBA16Float offscreen target
    rhi::TextureFormat colorFormat = rhi::TextureFormat::RGBA16Float;
    rhi::TextureFormat depthFormat = rhi::TextureFormat::Depth32Float;

    // Get native render pass for Linux (use HDR render pass)
    void* nativeRenderPass = nullptr;
#ifdef __linux__
    auto* vulkanSwapchain = dynamic_cast<RHI::Vulkan::VulkanRHISwapchain*>(swapchain);
    if (vulkanSwapchain) {
        nativeRenderPass = reinterpret_cast<void*>(
            static_cast<VkRenderPass>(vulkanSwapchain->getHDRRenderPass()));
    }
#endif

    if (particleRenderer->initialize(colorFormat, depthFormat, nativeRenderPass)) {
        LOG_INFO("Renderer") << "Particle renderer initialized successfully";
    } else {
        LOG_ERROR("Renderer") << "Failed to initialize particle renderer";
        particleRenderer.reset();
    }
}

// ============================================================================
// Phase 3.3: Skybox Renderer Creation
// ============================================================================

void Renderer::createSkyboxRenderer() {
    if (!rhiBridge || !rhiBridge->isReady()) {
        return;
    }

    auto* rhiDevice = rhiBridge->getDevice();
    auto* rhiQueue = rhiBridge->getGraphicsQueue();
    auto* swapchain = rhiBridge->getSwapchain();

    if (!rhiDevice || !rhiQueue || !swapchain) {
        return;
    }

    // Create skybox renderer
    skyboxRenderer = std::make_unique<rendering::SkyboxRenderer>(rhiDevice, rhiQueue);

    // All platforms: renders into HDR RGBA16Float offscreen target
    rhi::TextureFormat colorFormat = rhi::TextureFormat::RGBA16Float;
    rhi::TextureFormat depthFormat = rhi::TextureFormat::Depth32Float;

    // Get native render pass for Linux (use HDR render pass)
    void* nativeRenderPass = nullptr;
#ifdef __linux__
    auto* vulkanSwapchain = dynamic_cast<RHI::Vulkan::VulkanRHISwapchain*>(swapchain);
    if (vulkanSwapchain) {
        nativeRenderPass = reinterpret_cast<void*>(
            static_cast<VkRenderPass>(vulkanSwapchain->getHDRRenderPass()));
    }
#endif

    if (skyboxRenderer->initialize(colorFormat, depthFormat, nativeRenderPass)) {
        LOG_INFO("Renderer") << "Skybox renderer initialized successfully";
    } else {
        LOG_ERROR("Renderer") << "Failed to initialize skybox renderer";
        skyboxRenderer.reset();
    }
}

void Renderer::createShadowRenderer() {
    if (!rhiBridge || !rhiBridge->isReady()) {
        return;
    }

    auto* rhiDevice = rhiBridge->getDevice();
    auto* rhiQueue = rhiBridge->getGraphicsQueue();

    if (!rhiDevice || !rhiQueue) {
        return;
    }

    // Create shadow renderer
    shadowRenderer = std::make_unique<rendering::ShadowRenderer>(rhiDevice, rhiQueue);

    // Initialize shadow renderer (no native render pass needed - creates its own)
    if (shadowRenderer->initialize(nullptr, ssboBindGroupLayout.get())) {
        LOG_INFO("Renderer") << "Shadow renderer initialized successfully";

        // Update building bind groups with shadow map
        if (buildingBindGroupLayout && shadowRenderer->getShadowMapView() && shadowRenderer->getShadowSampler()) {
            buildingBindGroups.clear();
            for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
                rhi::BindGroupDesc bindGroupDesc;
                bindGroupDesc.layout = buildingBindGroupLayout.get();
                bindGroupDesc.entries.push_back(
                    rhi::BindGroupEntry::Buffer(0, rhiUniformBuffers[i].get())
                );
                bindGroupDesc.entries.push_back(
                    rhi::BindGroupEntry::TextureView(1, shadowRenderer->getShadowMapView())
                );
                bindGroupDesc.entries.push_back(
                    rhi::BindGroupEntry::Sampler(2, shadowRenderer->getShadowSampler())
                );
                // IBL bindings (3-6)
                if (iblManager && iblManager->isInitialized()) {
                    bindGroupDesc.entries.push_back(
                        rhi::BindGroupEntry::TextureView(3, iblManager->getIrradianceView())
                    );
                    bindGroupDesc.entries.push_back(
                        rhi::BindGroupEntry::TextureView(4, iblManager->getPrefilteredView())
                    );
                    bindGroupDesc.entries.push_back(
                        rhi::BindGroupEntry::TextureView(5, iblManager->getBRDFLutView())
                    );
                    bindGroupDesc.entries.push_back(
                        rhi::BindGroupEntry::Sampler(6, iblManager->getSampler())
                    );
                }
                bindGroupDesc.label = "Building Bind Group with Shadow + IBL";
                buildingBindGroups.push_back(rhiBridge->getDevice()->createBindGroup(bindGroupDesc));
            }
            LOG_INFO("Renderer") << "Building bind groups updated with shadow map";
        }
    } else {
        LOG_ERROR("Renderer") << "Failed to initialize shadow renderer";
        shadowRenderer.reset();
    }
}

void Renderer::createIBL() {
    if (!rhiBridge || !rhiBridge->isReady()) {
        return;
    }

    auto* rhiDevice = rhiBridge->getDevice();
    auto* rhiQueue = rhiBridge->getGraphicsQueue();

    if (!rhiDevice || !rhiQueue) {
        return;
    }

    iblManager = std::make_unique<rendering::IBLManager>(rhiDevice, rhiQueue);

    // For now, initialize with default (BRDF LUT only, no HDR env map yet)
    if (iblManager->initializeDefault()) {
        LOG_INFO("Renderer") << "IBL manager initialized (default mode)";
    } else {
        LOG_ERROR("Renderer") << "Failed to initialize IBL manager";
        iblManager.reset();
    }
}

bool Renderer::loadEnvironmentMap(const std::string& hdrPath) {
    if (!resourceManager || !iblManager) {
        LOG_ERROR("Renderer") << "Cannot load environment map: missing managers";
        return false;
    }

    // Load HDR texture
    rhi::RHITexture* hdrTexture = nullptr;
    try {
        hdrTexture = resourceManager->loadHDRTexture(hdrPath);
    } catch (const std::exception& e) {
        LOG_ERROR("Renderer") << "Failed to load HDR texture: " << e.what();
        return false;
    }

    if (!hdrTexture) {
        LOG_ERROR("Renderer") << "HDR texture is null";
        return false;
    }

    // Re-initialize IBL with the HDR environment
    iblManager = std::make_unique<rendering::IBLManager>(
        rhiBridge->getDevice(), rhiBridge->getGraphicsQueue());

    if (!iblManager->initialize(hdrTexture)) {
        LOG_ERROR("Renderer") << "Failed to initialize IBL with environment map";
        return false;
    }

    // Rebuild building bind groups with new IBL textures
    if (shadowRenderer && shadowRenderer->getShadowMapView() && shadowRenderer->getShadowSampler()) {
        buildingBindGroups.clear();
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            rhi::BindGroupDesc bindGroupDesc;
            bindGroupDesc.layout = buildingBindGroupLayout.get();
            bindGroupDesc.entries.push_back(
                rhi::BindGroupEntry::Buffer(0, rhiUniformBuffers[i].get())
            );
            bindGroupDesc.entries.push_back(
                rhi::BindGroupEntry::TextureView(1, shadowRenderer->getShadowMapView())
            );
            bindGroupDesc.entries.push_back(
                rhi::BindGroupEntry::Sampler(2, shadowRenderer->getShadowSampler())
            );
            bindGroupDesc.entries.push_back(
                rhi::BindGroupEntry::TextureView(3, iblManager->getIrradianceView())
            );
            bindGroupDesc.entries.push_back(
                rhi::BindGroupEntry::TextureView(4, iblManager->getPrefilteredView())
            );
            bindGroupDesc.entries.push_back(
                rhi::BindGroupEntry::TextureView(5, iblManager->getBRDFLutView())
            );
            bindGroupDesc.entries.push_back(
                rhi::BindGroupEntry::Sampler(6, iblManager->getSampler())
            );
            bindGroupDesc.label = "Building Bind Group with IBL";
            buildingBindGroups.push_back(rhiBridge->getDevice()->createBindGroup(bindGroupDesc));
        }
    }

    // Set skybox environment map
    if (skyboxRenderer) {
        skyboxRenderer->setEnvironmentMap(iblManager->getEnvironmentView(), iblManager->getSampler());
    }

    LOG_INFO("Renderer") << "Environment map loaded: " << hdrPath;
    return true;
}

// ============================================================================
// Phase 2.2: GPU Frustum Culling Pipeline
// ============================================================================
// Phase 3: G-Buffer + Deferred Lighting
// ============================================================================

#ifndef __EMSCRIPTEN__
void Renderer::createBindlessResources() {
    // Phase 4: Create bindless texture manager + 3 solid-colour material textures.
    // On lavapipe (Vulkan 1.1, no descriptor indexing) the manager reports isAvailable()=false
    // and the GBuffer pass falls back to procedural albedo — no visual change on this system.
    if (!rhiBridge || !rhiBridge->isReady()) return;
    auto* device = rhiBridge->getDevice();
    auto* queue  = rhiBridge->getGraphicsQueue();
    if (!device || !queue) return;

    bindlessTextureManager = std::make_unique<rendering::BindlessTextureManager>(device);
    if (!bindlessTextureManager->isAvailable()) {
        LOG_INFO("Renderer") << "Bindless textures unavailable on this device (graceful fallback)";
        return;
    }

    // --- Create a nearest-neighbour sampler for the 1×1 material textures ---
    rhi::SamplerDesc samplerDesc{};
    samplerDesc.minFilter  = rhi::FilterMode::Nearest;
    samplerDesc.magFilter  = rhi::FilterMode::Nearest;
    samplerDesc.mipmapFilter = rhi::MipmapMode::Nearest;
    samplerDesc.addressModeU = rhi::AddressMode::Repeat;
    samplerDesc.addressModeV = rhi::AddressMode::Repeat;
    bindlessSampler = device->createSampler(samplerDesc);
    if (!bindlessSampler) {
        LOG_ERROR("Renderer") << "createBindlessResources: failed to create sampler";
        return;
    }

    // --- Solid-colour 1×1 RGBA8 material textures ---
    //   Index 0: concrete / asphalt  (0.35, 0.35, 0.38, 1.0)
    //   Index 1: metal               (0.50, 0.52, 0.55, 1.0)
    //   Index 2: glass               (0.40, 0.58, 0.72, 1.0)
    const std::array<std::array<uint8_t, 4>, 3> colors = {{
        { 89,  89,  97, 255},   // concrete
        {127, 133, 140, 255},   // metal
        {102, 148, 184, 255},   // glass
    }};

    for (int i = 0; i < 3; ++i) {
        // Staging buffer
        rhi::BufferDesc stagingDesc{};
        stagingDesc.size  = 4;
        stagingDesc.usage = rhi::BufferUsage::CopySrc | rhi::BufferUsage::MapWrite;
        auto staging = device->createBuffer(stagingDesc);
        if (!staging) continue;

        void* mapped = staging->map();
        std::memcpy(mapped, colors[i].data(), 4);
        staging->unmap();

        // GPU texture
        rhi::TextureDesc texDesc{};
        texDesc.size          = rhi::Extent3D{1, 1, 1};
        texDesc.dimension     = rhi::TextureDimension::Texture2D;
        texDesc.format        = rhi::TextureFormat::RGBA8Unorm;
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount   = 1;
        texDesc.usage         = rhi::TextureUsage::CopyDst | rhi::TextureUsage::Sampled;
        bindlessMaterialTextures[i] = device->createTexture(texDesc);
        if (!bindlessMaterialTextures[i]) continue;

        // Upload
        auto encoder = device->createCommandEncoder();
        encoder->transitionTextureLayout(bindlessMaterialTextures[i].get(),
                                         rhi::TextureLayout::Undefined,
                                         rhi::TextureLayout::TransferDst);
        rhi::BufferTextureCopyInfo bufCopy{};
        bufCopy.buffer   = staging.get();
        rhi::TextureCopyInfo texCopy{};
        texCopy.texture  = bindlessMaterialTextures[i].get();
        texCopy.mipLevel = 0;
        encoder->copyBufferToTexture(bufCopy, texCopy, rhi::Extent3D{1, 1, 1});
        encoder->transitionTextureLayout(bindlessMaterialTextures[i].get(),
                                         rhi::TextureLayout::TransferDst,
                                         rhi::TextureLayout::ShaderReadOnly);
        auto cmd = encoder->finish();
        queue->submit(cmd.get());
        queue->waitIdle();

        // Texture view — created via RHITexture::createView()
        rhi::TextureViewDesc viewDesc{};
        viewDesc.format    = rhi::TextureFormat::RGBA8Unorm;
        viewDesc.dimension = rhi::TextureViewDimension::View2D;
        viewDesc.baseMipLevel   = 0;
        viewDesc.mipLevelCount  = 1;
        viewDesc.baseArrayLayer = 0;
        viewDesc.arrayLayerCount = 1;
        bindlessMaterialViews[i] = bindlessMaterialTextures[i]->createView(viewDesc);
        if (!bindlessMaterialViews[i]) continue;

        // Register in bindless manager
        uint32_t idx = bindlessTextureManager->registerTexture(
            bindlessMaterialViews[i].get(), bindlessSampler.get());
        LOG_INFO("Renderer") << "Material texture " << i << " registered at bindless slot " << idx;
    }

    LOG_INFO("Renderer") << "Bindless resources ready (" << bindlessTextureManager->isAvailable() << ")";
}
#endif  // !__EMSCRIPTEN__

#ifdef __EMSCRIPTEN__
void Renderer::createMaterialBindGroupInfrastructure() {
    if (materialBindGroupLayout) return;  // idempotent
    if (!rhiBridge || !rhiBridge->isReady() || !resourceManager) return;
    auto* device = rhiBridge->getDevice();
    if (!device) return;

    // ----------------------------------------------------------------------
    // Layout (set 2): 4 sampled textures + 1 shared sampler. All fragment-only.
    // ----------------------------------------------------------------------
    rhi::BindGroupLayoutDesc layoutDesc;
    layoutDesc.label = "MaterialBindGroupLayout";
    auto addTexEntry = [&](uint32_t binding) {
        rhi::BindGroupLayoutEntry e;
        e.binding              = binding;
        e.visibility           = rhi::ShaderStage::Fragment;
        e.type                 = rhi::BindingType::SampledTexture;
        e.textureViewDimension = rhi::TextureViewDimension::View2D;
        layoutDesc.entries.push_back(e);
    };
    addTexEntry(0);  // baseColor
    addTexEntry(1);  // normal
    addTexEntry(2);  // metallicRoughness
    addTexEntry(3);  // emissive
    addTexEntry(4);  // occlusion (AO)
    {
        rhi::BindGroupLayoutEntry samp;
        samp.binding    = 5;
        samp.visibility = rhi::ShaderStage::Fragment;
        samp.type       = rhi::BindingType::Sampler;
        layoutDesc.entries.push_back(samp);
    }
    materialBindGroupLayout = device->createBindGroupLayout(layoutDesc);
    if (!materialBindGroupLayout) {
        LOG_ERROR("Renderer") << "Failed to create material bind group layout";
        return;
    }

    // ----------------------------------------------------------------------
    // Shared sampler — linear filter, repeat. glTF samplers can override per
    // texture; the showcase uses one global default for now.
    // ----------------------------------------------------------------------
    {
        rhi::SamplerDesc sd;
        sd.magFilter      = rhi::FilterMode::Linear;
        sd.minFilter      = rhi::FilterMode::Linear;
        sd.mipmapFilter   = rhi::MipmapMode::Linear;
        sd.addressModeU   = rhi::AddressMode::Repeat;
        sd.addressModeV   = rhi::AddressMode::Repeat;
        sd.addressModeW   = rhi::AddressMode::Repeat;
        sd.maxAnisotropy  = 1;
        sd.label          = "MaterialSampler";
        materialSampler   = device->createSampler(sd);
    }

    // ----------------------------------------------------------------------
    // Dummy 1×1 textures. The shader multiplies each sampled value by an
    // ObjectData factor (baseColorFactor, metallicFactor, roughnessFactor,
    // ao factor) so the dummies must be IDENTITY values for that multiply:
    //   baseColor white      → sample × factor preserves the factor color
    //   normal flat (0,0,1)  → identity rotation in tangent space
    //   MR  (G=rough, B=met) → both channels = 1 so the factor passes through
    //   emissive black       → no self-emission added on top of lit color
    //   AO  (R=occlusion)    → R = 1 so the factor passes through
    // Note: an earlier draft of MR was (0,255,0,255) which forced metallic
    // to 0 across all buildings — bug, since their ObjectData carries a
    // nonzero metallic factor that should survive when no MR texture is
    // bound. The identity form below preserves the factor.
    // ----------------------------------------------------------------------
    auto upload1x1 = [&](uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                          rhi::TextureFormat fmt) {
        const uint8_t px[4] = { r, g, b, a };
        return resourceManager->uploadRGBA8FromMemory(px, 1, 1, fmt);
    };
    defaultBaseColorTex = upload1x1(255, 255, 255, 255, rhi::TextureFormat::RGBA8UnormSrgb);
    defaultNormalTex    = upload1x1(128, 128, 255, 255, rhi::TextureFormat::RGBA8Unorm);
    defaultMRTex        = upload1x1(  0, 255, 255, 255, rhi::TextureFormat::RGBA8Unorm);
    defaultEmissiveTex  = upload1x1(  0,   0,   0, 255, rhi::TextureFormat::RGBA8UnormSrgb);
    defaultAOTex        = upload1x1(255,   0,   0, 255, rhi::TextureFormat::RGBA8Unorm);
    if (!materialSampler || !defaultBaseColorTex || !defaultNormalTex
        || !defaultMRTex || !defaultEmissiveTex || !defaultAOTex) {
        LOG_ERROR("Renderer") << "Material default resources failed";
        return;
    }

    // Views — one per dummy texture, all 2D color aspect.
    auto makeView = [&](rhi::RHITexture* tex, rhi::TextureFormat fmt, const char* label) {
        rhi::TextureViewDesc vd;
        vd.dimension = rhi::TextureViewDimension::View2D;
        vd.format    = fmt;
        vd.label     = label;
        return tex->createView(vd);
    };
    defaultBaseColorView = makeView(defaultBaseColorTex.get(), rhi::TextureFormat::RGBA8UnormSrgb, "Default_BaseColorView");
    defaultNormalView    = makeView(defaultNormalTex.get(),    rhi::TextureFormat::RGBA8Unorm,     "Default_NormalView");
    defaultMRView        = makeView(defaultMRTex.get(),        rhi::TextureFormat::RGBA8Unorm,     "Default_MRView");
    defaultEmissiveView  = makeView(defaultEmissiveTex.get(),  rhi::TextureFormat::RGBA8UnormSrgb, "Default_EmissiveView");
    defaultAOView        = makeView(defaultAOTex.get(),        rhi::TextureFormat::RGBA8Unorm,     "Default_AOView");

    // ----------------------------------------------------------------------
    // Default material bind group (used by buildings and any showcase slot
    // that has no glTF texture supplied).
    // ----------------------------------------------------------------------
    rhi::BindGroupDesc bgDesc;
    bgDesc.layout = materialBindGroupLayout.get();
    bgDesc.label  = "DefaultMaterialBindGroup";
    bgDesc.entries.push_back(rhi::BindGroupEntry::TextureView(0, defaultBaseColorView.get()));
    bgDesc.entries.push_back(rhi::BindGroupEntry::TextureView(1, defaultNormalView.get()));
    bgDesc.entries.push_back(rhi::BindGroupEntry::TextureView(2, defaultMRView.get()));
    bgDesc.entries.push_back(rhi::BindGroupEntry::TextureView(3, defaultEmissiveView.get()));
    bgDesc.entries.push_back(rhi::BindGroupEntry::TextureView(4, defaultAOView.get()));
    bgDesc.entries.push_back(rhi::BindGroupEntry::Sampler    (5, materialSampler.get()));
    defaultMaterialBindGroup = device->createBindGroup(bgDesc);

    if (!defaultMaterialBindGroup) {
        LOG_ERROR("Renderer") << "Failed to create default material bind group";
        return;
    }

    LOG_INFO("Renderer") << "Material bind group infrastructure ready (set 2, WebGPU)";
}
#endif  // __EMSCRIPTEN__

void Renderer::createGBufferPass() {
    if (!rhiBridge || !rhiBridge->isReady()) return;
    auto* device = rhiBridge->getDevice();
    if (!device) return;

    uint32_t W = rhiBridge->getSwapchain()->getWidth();
    uint32_t H = rhiBridge->getSwapchain()->getHeight();

    VkDescriptorSetLayout bindlessLayout = VK_NULL_HANDLE;
#ifndef __EMSCRIPTEN__
    if (bindlessTextureManager && bindlessTextureManager->isAvailable())
        bindlessLayout = bindlessTextureManager->getVkDescriptorSetLayout();
#endif

    rhi::RHIBindGroupLayout* matLayout = nullptr;
#ifdef __EMSCRIPTEN__
    matLayout = materialBindGroupLayout.get();
#endif

    gBufferPass = std::make_unique<rendering::GBufferPass>(device);
    if (!gBufferPass->initialize(W, H,
                                  buildingBindGroupLayout.get(),
                                  ssboBindGroupLayout.get(),
                                  rhiDepthImageView.get(),
                                  bindlessLayout,
                                  matLayout)) {
        LOG_ERROR("Renderer") << "GBufferPass initialization failed";
        gBufferPass.reset();
    } else {
        LOG_INFO("Renderer") << "GBufferPass initialized";
    }
}

void Renderer::createDeferredLightingPass() {
    if (!rhiBridge || !rhiBridge->isReady()) return;
    if (!gBufferPass || !gBufferPass->isInitialized()) return;
    if (!shadowRenderer || !shadowRenderer->isInitialized()) return;
    if (!iblManager || !iblManager->isInitialized()) return;

    auto* device = rhiBridge->getDevice();
    if (!device) return;

    // Collect per-frame UBO pointers
    std::array<rhi::RHIBuffer*, rendering::DeferredLightingPass::MAX_FRAMES_IN_FLIGHT> ubos{};
    for (size_t i = 0; i < rendering::DeferredLightingPass::MAX_FRAMES_IN_FLIGHT && i < rhiUniformBuffers.size(); ++i)
        ubos[i] = rhiUniformBuffers[i].get();

    void* nativeRenderPass = nullptr;
#ifdef __linux__
    auto* rhiVulkanSC = dynamic_cast<RHI::Vulkan::VulkanRHISwapchain*>(rhiBridge->getSwapchain());
    if (rhiVulkanSC)
        // Use the Load variant render pass so the pipeline is compatible with LoadOp::Load
        nativeRenderPass = reinterpret_cast<void*>(static_cast<VkRenderPass>(rhiVulkanSC->getHDRLoadRenderPass()));
#endif

    deferredLightingPass = std::make_unique<rendering::DeferredLightingPass>(device);
    if (!deferredLightingPass->initialize(
            ubos, sizeof(UniformBufferObject),
            gBufferPass->getGBuffer0View(),
            gBufferPass->getGBuffer1View(),
            gBufferPass->getGBuffer2View(),
            rhiDepthImageView.get(),
            gBufferPass->getSampler(),
            shadowRenderer->getShadowMapView(),
            shadowRenderer->getShadowSampler(),
            iblManager->getIrradianceView(),
            iblManager->getPrefilteredView(),
            iblManager->getBRDFLutView(),
            iblManager->getSampler(),
            nativeRenderPass)) {
        LOG_ERROR("Renderer") << "DeferredLightingPass initialization failed";
        deferredLightingPass.reset();
    } else {
        LOG_INFO("Renderer") << "DeferredLightingPass initialized";
    }
}

// ============================================================================

void Renderer::createCullingPipeline() {
    auto* device = rhiBridge->getDevice();

    // Load compute shader
#ifdef __EMSCRIPTEN__
    std::string path = "shaders/frustum_cull.comp.wgsl";
    auto codeRaw = FileUtils::readFile(path);
    if (codeRaw.empty()) {
        LOG_ERROR("Renderer") << "Failed to load " << path;
        return;
    }
    std::vector<uint8_t> code(codeRaw.begin(), codeRaw.end());
    rhi::ShaderSource source(rhi::ShaderLanguage::WGSL, code, rhi::ShaderStage::Compute, "main");
#else
    std::string path = "shaders/frustum_cull.comp.spv";
    auto codeRaw = FileUtils::readFile(path);
    if (codeRaw.empty()) {
        LOG_ERROR("Renderer") << "Failed to load " << path;
        return;
    }
    std::vector<uint8_t> code(codeRaw.begin(), codeRaw.end());
    rhi::ShaderSource source(rhi::ShaderLanguage::SPIRV, code, rhi::ShaderStage::Compute, "main");
#endif

    rhi::ShaderDesc shaderDesc(source, "frustum_cull_compute");
    cullComputeShader = device->createShader(shaderDesc);
    if (!cullComputeShader) {
        LOG_ERROR("Renderer") << "Failed to create frustum cull compute shader";
        return;
    }

    // Create cull bind group layout (4 entries, all Compute visibility)
    rhi::BindGroupLayoutDesc cullLayoutDesc;

    // Binding 0: CullUBO (uniform)
    rhi::BindGroupLayoutEntry cullUboEntry;
    cullUboEntry.binding = 0;
    cullUboEntry.visibility = rhi::ShaderStage::Compute;
    cullUboEntry.type = rhi::BindingType::UniformBuffer;
    cullLayoutDesc.entries.push_back(cullUboEntry);

    // Binding 1: ObjectData[] (storage, read) — must match WGSL var<storage, read>
    rhi::BindGroupLayoutEntry objEntry;
    objEntry.binding = 1;
    objEntry.visibility = rhi::ShaderStage::Compute;
    objEntry.type = rhi::BindingType::ReadOnlyStorageBuffer;
    cullLayoutDesc.entries.push_back(objEntry);

    // Binding 2: IndirectDrawCommand (storage, read_write)
    rhi::BindGroupLayoutEntry indirectEntry;
    indirectEntry.binding = 2;
    indirectEntry.visibility = rhi::ShaderStage::Compute;
    indirectEntry.type = rhi::BindingType::StorageBuffer;
    cullLayoutDesc.entries.push_back(indirectEntry);

    // Binding 3: VisibleIndices[] (storage, write)
    rhi::BindGroupLayoutEntry visIndicesEntry;
    visIndicesEntry.binding = 3;
    visIndicesEntry.visibility = rhi::ShaderStage::Compute;
    visIndicesEntry.type = rhi::BindingType::StorageBuffer;
    cullLayoutDesc.entries.push_back(visIndicesEntry);

    cullLayoutDesc.label = "Cull Bind Group Layout";
    cullBindGroupLayout = device->createBindGroupLayout(cullLayoutDesc);
    if (!cullBindGroupLayout) {
        LOG_ERROR("Renderer") << "Failed to create cull bind group layout";
        return;
    }

    // Create pipeline layout
    rhi::PipelineLayoutDesc plDesc;
    plDesc.bindGroupLayouts = {cullBindGroupLayout.get()};
    cullPipelineLayout = device->createPipelineLayout(plDesc);
    if (!cullPipelineLayout) {
        LOG_ERROR("Renderer") << "Failed to create cull pipeline layout";
        return;
    }

    // Create compute pipeline
    rhi::ComputePipelineDesc cpDesc(cullComputeShader.get(), cullPipelineLayout.get());
    cpDesc.label = "Frustum_Cull_Pipeline";
    cullPipeline = device->createComputePipeline(cpDesc);
    if (!cullPipeline) {
        LOG_ERROR("Renderer") << "Failed to create frustum cull compute pipeline";
        return;
    }

    // Create per-frame buffers
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        // CullUBO: 112 bytes (6 * vec4 + objectCount + indexCount + pad)
        rhi::BufferDesc uboDesc;
        uboDesc.size = sizeof(CullUBO);
        uboDesc.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::MapWrite;
        uboDesc.label = "Cull UBO";
        cullUniformBuffers[i] = device->createBuffer(uboDesc);

        // Indirect draw buffer: 20 bytes (DrawIndexedIndirectCommand)
        // Phase 3.2: Enable concurrent sharing for async compute
        const auto& features = device->getCapabilities().getFeatures();
        bool needsConcurrent = features.dedicatedComputeQueue && features.timelineSemaphores;

        rhi::BufferDesc indirectDesc;
        indirectDesc.size = 20;  // 5 * uint32_t
        indirectDesc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Indirect | rhi::BufferUsage::MapWrite;
        indirectDesc.label = "Indirect Draw Buffer";
        indirectDesc.concurrentSharing = needsConcurrent;
        indirectDrawBuffers[i] = device->createBuffer(indirectDesc);

        // Visible indices buffer: 4 bytes per object
        rhi::BufferDesc visDesc;
        visDesc.size = sizeof(uint32_t) * MAX_CULL_OBJECTS;
        visDesc.usage = rhi::BufferUsage::Storage;
        visDesc.label = "Visible Indices Buffer";
        visDesc.concurrentSharing = needsConcurrent;
        visibleIndicesBuffers[i] = device->createBuffer(visDesc);
    }

    LOG_INFO("Renderer") << "GPU frustum culling pipeline created";
}

void Renderer::extractFrustumPlanes(const glm::mat4& vp, glm::vec4 planes[6]) {
    // Griggs-Hartmann frustum plane extraction from VP matrix
    // GLM is column-major: vp[col][row]
    // Left
    planes[0] = glm::vec4(
        vp[0][3] + vp[0][0],
        vp[1][3] + vp[1][0],
        vp[2][3] + vp[2][0],
        vp[3][3] + vp[3][0]
    );
    // Right
    planes[1] = glm::vec4(
        vp[0][3] - vp[0][0],
        vp[1][3] - vp[1][0],
        vp[2][3] - vp[2][0],
        vp[3][3] - vp[3][0]
    );
    // Bottom
    planes[2] = glm::vec4(
        vp[0][3] + vp[0][1],
        vp[1][3] + vp[1][1],
        vp[2][3] + vp[2][1],
        vp[3][3] + vp[3][1]
    );
    // Top
    planes[3] = glm::vec4(
        vp[0][3] - vp[0][1],
        vp[1][3] - vp[1][1],
        vp[2][3] - vp[2][1],
        vp[3][3] - vp[3][1]
    );
    // Near
    planes[4] = glm::vec4(
        vp[0][3] + vp[0][2],
        vp[1][3] + vp[1][2],
        vp[2][3] + vp[2][2],
        vp[3][3] + vp[3][2]
    );
    // Far
    planes[5] = glm::vec4(
        vp[0][3] - vp[0][2],
        vp[1][3] - vp[1][2],
        vp[2][3] - vp[2][2],
        vp[3][3] - vp[3][2]
    );

    // Normalize each plane
    for (int i = 0; i < 6; i++) {
        float len = glm::length(glm::vec3(planes[i]));
        if (len > 0.0f) {
            planes[i] /= len;
        }
    }
}

// ============================================================================
// HDR Render Target Creation (all platforms)
// ============================================================================

void Renderer::createHDRRenderTarget() {
    if (!rhiBridge || !rhiBridge->isReady()) {
        return;
    }

    auto* rhiDevice = rhiBridge->getDevice();
    auto* swapchain = rhiBridge->getSwapchain();
    if (!rhiDevice || !swapchain) {
        return;
    }

    uint32_t width = swapchain->getWidth();
    uint32_t height = swapchain->getHeight();

    // Create RGBA16Float HDR color texture (geometry renders here instead of swapchain)
    rhi::TextureDesc colorDesc;
    colorDesc.size = rhi::Extent3D(width, height, 1);
    colorDesc.format = rhi::TextureFormat::RGBA16Float;
    colorDesc.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled;
    colorDesc.label = "HDR Color Target";
    hdrColorTexture = rhiDevice->createTexture(colorDesc);

    if (hdrColorTexture) {
        rhi::TextureViewDesc viewDesc;
        viewDesc.format = rhi::TextureFormat::RGBA16Float;
        viewDesc.dimension = rhi::TextureViewDimension::View2D;
        hdrColorView = hdrColorTexture->createView(viewDesc);
        LOG_INFO("Renderer") << "HDR render target created (" << width << "x" << height << ")";
    } else {
        LOG_ERROR("Renderer") << "Failed to create HDR color texture";
        return;
    }

    // Create sampler shared by tonemap and FXAA passes
    rhi::SamplerDesc samplerDesc;
    samplerDesc.magFilter = rhi::FilterMode::Linear;
    samplerDesc.minFilter = rhi::FilterMode::Linear;
    samplerDesc.addressModeU = rhi::AddressMode::ClampToEdge;
    samplerDesc.addressModeV = rhi::AddressMode::ClampToEdge;
    samplerDesc.label = "Post-process Sampler";
    hdrSampler = rhiDevice->createSampler(samplerDesc);

#ifdef __EMSCRIPTEN__
    // WebGPU: no LDR intermediate buffer — postprocess.wgsl writes directly to swapchain
#else
    // Vulkan: create HDR + post-process render passes and framebuffers on Linux
#ifdef __linux__
    auto* vulkanSwapchain = dynamic_cast<RHI::Vulkan::VulkanRHISwapchain*>(swapchain);
    if (vulkanSwapchain && hdrColorView && rhiDepthImageView) {
        auto* hdrVkView   = dynamic_cast<RHI::Vulkan::VulkanRHITextureView*>(hdrColorView.get());
        auto* depthVkView = dynamic_cast<RHI::Vulkan::VulkanRHITextureView*>(rhiDepthImageView.get());
        if (hdrVkView && depthVkView) {
            vulkanSwapchain->createHDRRenderPass();
            vulkanSwapchain->createHDRLoadRenderPass();   // load variant for DeferredLighting
            vulkanSwapchain->createHDRFramebuffer(hdrVkView->getVkImageView(),
                                                  depthVkView->getVkImageView());
            vulkanSwapchain->createPostProcessRenderPass();
            vulkanSwapchain->createPostProcessFramebuffers();
        }
    }
#endif
#endif
}

// ============================================================================
// Bloom Render Pipelines (WebGPU only)
// ============================================================================
#ifdef __EMSCRIPTEN__

void Renderer::createBloomPipelineWGSL() {
    if (!rhiBridge || !rhiBridge->isReady() || !hdrColorView || !hdrSampler) return;
    auto* device = rhiBridge->getDevice();

    uint32_t bW = std::max(1u, rhiBridge->getSwapchain()->getWidth()  / 2);
    uint32_t bH = std::max(1u, rhiBridge->getSwapchain()->getHeight() / 2);

    // Half-res RGBA16Float bloom textures (render target + sampled)
    rhi::TextureDesc td;
    td.size   = rhi::Extent3D(bW, bH, 1);
    td.format = rhi::TextureFormat::RGBA16Float;
    td.usage  = rhi::TextureUsage::Sampled | rhi::TextureUsage::RenderTarget;
    td.mipLevelCount = 1; td.arrayLayerCount = 1;
    td.label = "BloomTexture";
    bloomTexture = device->createTexture(td);
    td.label = "BloomPingTexture";
    bloomPingTexture = device->createTexture(td);
    if (!bloomTexture || !bloomPingTexture) {
        LOG_ERROR("Renderer") << "[WebGPU Bloom] Failed to create bloom textures";
        return;
    }

    rhi::TextureViewDesc vd;
    vd.format    = rhi::TextureFormat::RGBA16Float;
    vd.dimension = rhi::TextureViewDimension::View2D;
    vd.label = "BloomTextureView";
    bloomTextureView = bloomTexture->createView(vd);
    vd.label = "BloomPingView";
    bloomPingView = bloomPingTexture->createView(vd);

    rhi::SamplerDesc sd;
    sd.magFilter    = rhi::FilterMode::Linear;
    sd.minFilter    = rhi::FilterMode::Linear;
    sd.addressModeU = rhi::AddressMode::ClampToEdge;
    sd.addressModeV = rhi::AddressMode::ClampToEdge;
    bloomSampler = device->createSampler(sd);

    if (!bloomTextureView || !bloomPingView || !bloomSampler) {
        LOG_ERROR("Renderer") << "[WebGPU Bloom] Failed to create bloom views/sampler";
        return;
    }

    // Shared bind group layout: {SampledTexture(0), Sampler(1)}
    rhi::BindGroupLayoutDesc layoutDesc;
    layoutDesc.entries = {
        rhi::BindGroupLayoutEntry(0, rhi::ShaderStage::Fragment, rhi::BindingType::SampledTexture),
        rhi::BindGroupLayoutEntry(1, rhi::ShaderStage::Fragment, rhi::BindingType::Sampler),
    };
    layoutDesc.label = "WGSLBloomLayout";
    wgslBloomLayout = device->createBindGroupLayout(layoutDesc);
    if (!wgslBloomLayout) { LOG_ERROR("Renderer") << "[WebGPU Bloom] Layout creation failed"; return; }

    rhi::PipelineLayoutDesc plDesc;
    plDesc.bindGroupLayouts = { wgslBloomLayout.get() };
    plDesc.label = "WGSLBloomPipelineLayout";
    wgslBloomPipelineLayout = device->createPipelineLayout(plDesc);
    if (!wgslBloomPipelineLayout) { LOG_ERROR("Renderer") << "[WebGPU Bloom] Pipeline layout failed"; return; }

    // RGBA16Float color target (half-res)
    rhi::ColorTargetState bloomCT;
    bloomCT.format = rhi::TextureFormat::RGBA16Float;
    bloomCT.blend.blendEnabled = false;

    // Helper: create a fullscreen render pipeline from a WGSL file + entry point
    auto makeBloomPipeline = [&](const char* path, const char* fsEntry, const char* label)
        -> std::unique_ptr<rhi::RHIRenderPipeline>
    {
        auto raw = FileUtils::readFile(path);
        if (raw.empty()) { LOG_ERROR("Renderer") << "[WebGPU Bloom] Failed to load " << path; return {}; }
        std::vector<uint8_t> code(raw.begin(), raw.end());
        auto vs = device->createShader(rhi::ShaderDesc(
            rhi::ShaderSource(rhi::ShaderLanguage::WGSL, code, rhi::ShaderStage::Vertex, "vs_main"), label));
        auto fs = device->createShader(rhi::ShaderDesc(
            rhi::ShaderSource(rhi::ShaderLanguage::WGSL, code, rhi::ShaderStage::Fragment, fsEntry), label));
        if (!vs || !fs) return {};
        rhi::RenderPipelineDesc pd;
        pd.label          = label;
        pd.layout         = wgslBloomPipelineLayout.get();
        pd.vertexShader   = vs.get();
        pd.fragmentShader = fs.get();
        pd.primitive.topology  = rhi::PrimitiveTopology::TriangleList;
        pd.primitive.cullMode  = rhi::CullMode::None;
        pd.primitive.frontFace = rhi::FrontFace::CounterClockwise;
        pd.colorTargets = { bloomCT };
        pd.depthStencil = nullptr;
        return device->createRenderPipeline(pd);
    };

    wgslBloomPrefilterPipeline = makeBloomPipeline("shaders/bloom_prefilter.wgsl", "fs_main",       "BloomPrefilter");
    wgslBloomBlurHPipeline     = makeBloomPipeline("shaders/bloom_blur.wgsl",      "fs_horizontal", "BloomBlurH");
    wgslBloomBlurVPipeline     = makeBloomPipeline("shaders/bloom_blur.wgsl",      "fs_vertical",   "BloomBlurV");

    if (!wgslBloomPrefilterPipeline || !wgslBloomBlurHPipeline || !wgslBloomBlurVPipeline) {
        LOG_ERROR("Renderer") << "[WebGPU Bloom] Pipeline creation failed";
        return;
    }

    // Bind groups
    auto makeBG = [&](rhi::RHITextureView* tex, rhi::RHISampler* samp, const char* lbl)
        -> std::unique_ptr<rhi::RHIBindGroup>
    {
        rhi::BindGroupDesc d;
        d.layout = wgslBloomLayout.get();
        d.entries = {
            rhi::BindGroupEntry::TextureView(0, tex),
            rhi::BindGroupEntry::Sampler(1, samp),
        };
        d.label = lbl;
        return device->createBindGroup(d);
    };

    wgslBloomPrefilterBG = makeBG(hdrColorView.get(), hdrSampler.get(), "BloomPrefilterBG");
    wgslBloomBlurBGs[0]  = makeBG(bloomTextureView.get(), bloomSampler.get(), "BloomBlurBG_H");
    wgslBloomBlurBGs[1]  = makeBG(bloomPingView.get(),    bloomSampler.get(), "BloomBlurBG_V");

    LOG_INFO("Renderer") << "[WebGPU Bloom] Initialized " << bW << "x" << bH;
}

// ============================================================================
// SSAO Render Pipelines (WebGPU — render-pass based, R8Unorm color attachment)
// ============================================================================

void Renderer::createSSAOPipelineWGSL() {
    if (!rhiBridge || !rhiBridge->isReady() || !rhiDepthImageView || !hdrSampler) return;
    auto* device   = rhiBridge->getDevice();
    auto* swapchain = rhiBridge->getSwapchain();
    if (!swapchain) return;

    uint32_t ssaoW = std::max(1u, swapchain->getWidth()  / 2);
    uint32_t ssaoH = std::max(1u, swapchain->getHeight() / 2);

    // Half-res R8Unorm SSAO textures (RenderTarget + Sampled — R8Unorm storage not supported in WebGPU)
    auto makeSSAOTex = [&](const char* lbl) {
        rhi::TextureDesc d;
        d.format = rhi::TextureFormat::R8Unorm;
        d.size   = rhi::Extent3D(ssaoW, ssaoH, 1);
        d.usage  = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled;
        d.label  = lbl;
        return device->createTexture(d);
    };
    ssaoTexture     = makeSSAOTex("SSAO Texture");
    ssaoBlurTexture = makeSSAOTex("SSAO Blur Texture");
    if (!ssaoTexture || !ssaoBlurTexture) {
        LOG_ERROR("Renderer") << "[WebGPU SSAO] Failed to create SSAO textures"; return;
    }

    rhi::TextureViewDesc vd;
    vd.format    = rhi::TextureFormat::R8Unorm;
    vd.dimension = rhi::TextureViewDimension::View2D;
    vd.label = "SSAOTextureView";
    ssaoTextureView = ssaoTexture->createView(vd);
    vd.label = "SSAOBlurView";
    ssaoBlurView    = ssaoBlurTexture->createView(vd);

    // All three filters must be Nearest for WebGPU to classify this as a non-filtering sampler
    // (required when sampling depth textures via textureSampleLevel in SSAO).
    rhi::SamplerDesc sd;
    sd.magFilter = sd.minFilter = rhi::FilterMode::Nearest;
    sd.mipmapFilter = rhi::MipmapMode::Nearest;
    sd.addressModeU = sd.addressModeV = rhi::AddressMode::ClampToEdge;
    ssaoSampler = device->createSampler(sd);

    if (!ssaoTextureView || !ssaoBlurView || !ssaoSampler) {
        LOG_ERROR("Renderer") << "[WebGPU SSAO] Failed to create SSAO views/sampler"; return;
    }

    // Uniform buffers
    {
        rhi::BufferDesc bd;
        bd.size = 96;  // sizeof(SSAOParams): mat4(64) + 8×f32(32)
        bd.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::MapWrite;
        bd.mappedAtCreation = false;
        bd.label = "SSAOParamsUBO";
        wgslSSAOParamsUBO = device->createBuffer(bd);
        bd.size = 32;  // sizeof(SSAOBlurParams): 8×f32
        bd.label = "SSAOBlurParamsUBO";
        wgslSSAOBlurParamsUBO = device->createBuffer(bd);
    }
    if (!wgslSSAOParamsUBO || !wgslSSAOBlurParamsUBO) {
        LOG_ERROR("Renderer") << "[WebGPU SSAO] Failed to create UBOs"; return;
    }

    using S = rhi::ShaderStage;
    using T = rhi::BindingType;

    // ---- SSAO pass layout: depth, sampler, uniform ----
    {
        rhi::BindGroupLayoutDesc ld;
        ld.label = "WGSLSSAOLayout";
        ld.entries = {
            rhi::BindGroupLayoutEntry(0, S::Fragment, T::DepthTexture),
            rhi::BindGroupLayoutEntry(1, S::Fragment, T::NonFilteringSampler),
            rhi::BindGroupLayoutEntry(2, S::Fragment, T::UniformBuffer),
        };
        wgslSSAOLayout = device->createBindGroupLayout(ld);
    }
    if (!wgslSSAOLayout) { LOG_ERROR("Renderer") << "[WebGPU SSAO] Layout failed"; return; }

    {
        rhi::PipelineLayoutDesc pld;
        pld.bindGroupLayouts = { wgslSSAOLayout.get() };
        pld.label = "WGSLSSAOPipelineLayout";
        wgslSSAOPipelineLayout = device->createPipelineLayout(pld);
    }
    if (!wgslSSAOPipelineLayout) { LOG_ERROR("Renderer") << "[WebGPU SSAO] Pipeline layout failed"; return; }

    {
        rhi::BindGroupDesc bd;
        bd.layout = wgslSSAOLayout.get();
        bd.label  = "WGSLSSAOBindGroup";
        bd.entries = {
            rhi::BindGroupEntry::TextureView(0, rhiDepthImageView.get()),
            rhi::BindGroupEntry::Sampler(1, ssaoSampler.get()),
            rhi::BindGroupEntry::Buffer(2, wgslSSAOParamsUBO.get(), 0, 96),
        };
        wgslSSAOBG = device->createBindGroup(bd);
    }
    if (!wgslSSAOBG) { LOG_ERROR("Renderer") << "[WebGPU SSAO] Bind group failed"; return; }

    // SSAO pipeline (depth → R8Unorm AO)
    {
        auto raw = FileUtils::readFile("shaders/ssao.wgsl");
        if (raw.empty()) { LOG_ERROR("Renderer") << "[WebGPU SSAO] Failed to load ssao.wgsl"; return; }
        std::vector<uint8_t> code(raw.begin(), raw.end());
        auto vs = device->createShader(rhi::ShaderDesc(
            rhi::ShaderSource(rhi::ShaderLanguage::WGSL, code, rhi::ShaderStage::Vertex, "vs_main"), "SSAO_VS"));
        auto fs = device->createShader(rhi::ShaderDesc(
            rhi::ShaderSource(rhi::ShaderLanguage::WGSL, code, rhi::ShaderStage::Fragment, "fs_main"), "SSAO_FS"));
        if (!vs || !fs) { LOG_ERROR("Renderer") << "[WebGPU SSAO] Shader creation failed"; return; }
        rhi::RenderPipelineDesc pd;
        pd.label          = "SSAOPipeline";
        pd.layout         = wgslSSAOPipelineLayout.get();
        pd.vertexShader   = vs.get();
        pd.fragmentShader = fs.get();
        pd.primitive.topology  = rhi::PrimitiveTopology::TriangleList;
        pd.primitive.cullMode  = rhi::CullMode::None;
        pd.primitive.frontFace = rhi::FrontFace::CounterClockwise;
        rhi::ColorTargetState ct; ct.format = rhi::TextureFormat::R8Unorm; ct.blend.blendEnabled = false;
        pd.colorTargets   = { ct };
        pd.depthStencil   = nullptr;
        wgslSSAOPipeline  = device->createRenderPipeline(pd);
    }
    if (!wgslSSAOPipeline) { LOG_ERROR("Renderer") << "[WebGPU SSAO] Pipeline creation failed"; return; }

    // ---- SSAO blur pass layout: aoTex(sampled), depth(DepthTexture), sampler, uniform ----
    {
        rhi::BindGroupLayoutDesc ld;
        ld.label = "WGSLSSAOBlurLayout";
        ld.entries = {
            rhi::BindGroupLayoutEntry(0, S::Fragment, T::SampledTexture),
            rhi::BindGroupLayoutEntry(1, S::Fragment, T::DepthTexture),
            rhi::BindGroupLayoutEntry(2, S::Fragment, T::NonFilteringSampler),
            rhi::BindGroupLayoutEntry(3, S::Fragment, T::UniformBuffer),
        };
        wgslSSAOBlurLayout = device->createBindGroupLayout(ld);
    }
    if (!wgslSSAOBlurLayout) { LOG_ERROR("Renderer") << "[WebGPU SSAO] Blur layout failed"; return; }

    {
        rhi::PipelineLayoutDesc pld;
        pld.bindGroupLayouts = { wgslSSAOBlurLayout.get() };
        pld.label = "WGSLSSAOBlurPipelineLayout";
        wgslSSAOBlurPipelineLayout = device->createPipelineLayout(pld);
    }
    if (!wgslSSAOBlurPipelineLayout) { LOG_ERROR("Renderer") << "[WebGPU SSAO] Blur pipeline layout failed"; return; }

    {
        rhi::BindGroupDesc bd;
        bd.layout = wgslSSAOBlurLayout.get();
        bd.label  = "WGSLSSAOBlurBindGroup";
        bd.entries = {
            rhi::BindGroupEntry::TextureView(0, ssaoTextureView.get()),
            rhi::BindGroupEntry::TextureView(1, rhiDepthImageView.get()),
            rhi::BindGroupEntry::Sampler(2, ssaoSampler.get()),
            rhi::BindGroupEntry::Buffer(3, wgslSSAOBlurParamsUBO.get(), 0, 32),
        };
        wgslSSAOBlurBG = device->createBindGroup(bd);
    }
    if (!wgslSSAOBlurBG) { LOG_ERROR("Renderer") << "[WebGPU SSAO] Blur bind group failed"; return; }

    // SSAO blur pipeline (AO + depth → blurred R8Unorm AO)
    {
        auto raw = FileUtils::readFile("shaders/ssao_blur.wgsl");
        if (raw.empty()) { LOG_ERROR("Renderer") << "[WebGPU SSAO] Failed to load ssao_blur.wgsl"; return; }
        std::vector<uint8_t> code(raw.begin(), raw.end());
        auto vs = device->createShader(rhi::ShaderDesc(
            rhi::ShaderSource(rhi::ShaderLanguage::WGSL, code, rhi::ShaderStage::Vertex, "vs_main"), "SSAOBlur_VS"));
        auto fs = device->createShader(rhi::ShaderDesc(
            rhi::ShaderSource(rhi::ShaderLanguage::WGSL, code, rhi::ShaderStage::Fragment, "fs_main"), "SSAOBlur_FS"));
        if (!vs || !fs) { LOG_ERROR("Renderer") << "[WebGPU SSAO] Blur shader creation failed"; return; }
        rhi::RenderPipelineDesc pd;
        pd.label          = "SSAOBlurPipeline";
        pd.layout         = wgslSSAOBlurPipelineLayout.get();
        pd.vertexShader   = vs.get();
        pd.fragmentShader = fs.get();
        pd.primitive.topology  = rhi::PrimitiveTopology::TriangleList;
        pd.primitive.cullMode  = rhi::CullMode::None;
        pd.primitive.frontFace = rhi::FrontFace::CounterClockwise;
        rhi::ColorTargetState ct; ct.format = rhi::TextureFormat::R8Unorm; ct.blend.blendEnabled = false;
        pd.colorTargets        = { ct };
        pd.depthStencil        = nullptr;
        wgslSSAOBlurPipeline   = device->createRenderPipeline(pd);
    }
    if (!wgslSSAOBlurPipeline) { LOG_ERROR("Renderer") << "[WebGPU SSAO] Blur pipeline creation failed"; return; }

    LOG_INFO("Renderer") << "[WebGPU SSAO] Initialized " << ssaoW << "x" << ssaoH;
}

// ============================================================================
// Tonemap Pipeline Creation (WebGPU only)
// ============================================================================

void Renderer::createPostProcessPipelineWGSL() {
    if (!rhiBridge || !rhiBridge->isReady() || !hdrColorView || !hdrSampler) return;

    auto* device = rhiBridge->getDevice();

    auto raw = FileUtils::readFile("shaders/postprocess.wgsl");
    if (raw.empty()) { LOG_ERROR("Renderer") << "Failed to read postprocess.wgsl"; return; }
    std::vector<uint8_t> code(raw.begin(), raw.end());

    rhi::ShaderSource vs(rhi::ShaderLanguage::WGSL, code, rhi::ShaderStage::Vertex,   "vs_main");
    rhi::ShaderSource fs(rhi::ShaderLanguage::WGSL, code, rhi::ShaderStage::Fragment, "fs_main");
    wgslPostprocessVertexShader   = device->createShader(rhi::ShaderDesc(vs, "PostProcessVS"));
    wgslPostprocessFragmentShader = device->createShader(rhi::ShaderDesc(fs, "PostProcessFS"));
    if (!wgslPostprocessVertexShader || !wgslPostprocessFragmentShader) {
        LOG_ERROR("Renderer") << "Failed to create postprocess shaders"; return;
    }

    // Params UBO: 48 bytes (8 floats + abSplitX + 3 pad → std140 16-byte aligned)
    {
        rhi::BufferDesc bd;
        bd.size  = 48;
        bd.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDst;
        bd.label = "PostProcessParamsUBO";
        wgslPostprocessParamsUBO = device->createBuffer(bd);
    }
    if (!wgslPostprocessParamsUBO) { LOG_ERROR("Renderer") << "Failed to create postprocess UBO"; return; }

    // Bind group layout: 0=hdr, 1=bloom, 2=ssao, 3=sampler, 4=params UBO
    using S = rhi::ShaderStage; using T = rhi::BindingType;
    rhi::BindGroupLayoutDesc ld;
    ld.entries = {
        rhi::BindGroupLayoutEntry(0, S::Fragment, T::SampledTexture),   // hdrTexture
        rhi::BindGroupLayoutEntry(1, S::Fragment, T::SampledTexture),   // bloomTexture
        rhi::BindGroupLayoutEntry(2, S::Fragment, T::SampledTexture),   // ssaoTexture
        rhi::BindGroupLayoutEntry(3, S::Fragment, T::Sampler),          // hdrSampler
        rhi::BindGroupLayoutEntry(4, S::Fragment, T::UniformBuffer),    // params UBO
    };
    ld.label = "PostProcess Layout";
    wgslPostprocessLayout = device->createBindGroupLayout(ld);
    if (!wgslPostprocessLayout) { LOG_ERROR("Renderer") << "PostProcess layout failed"; return; }

    // Bind group
    rhi::RHITextureView* bv   = bloomTextureView ? bloomTextureView.get() : hdrColorView.get();
    rhi::RHITextureView* ssao = ssaoBlurView     ? ssaoBlurView.get()     : hdrColorView.get();
    rhi::BindGroupDesc bd;
    bd.layout = wgslPostprocessLayout.get();
    bd.entries = {
        rhi::BindGroupEntry::TextureView(0, hdrColorView.get()),
        rhi::BindGroupEntry::TextureView(1, bv),
        rhi::BindGroupEntry::TextureView(2, ssao),
        rhi::BindGroupEntry::Sampler    (3, hdrSampler.get()),
        rhi::BindGroupEntry::Buffer     (4, wgslPostprocessParamsUBO.get(), 0, 48),
    };
    bd.label = "PostProcess BG";
    wgslPostprocessBG = device->createBindGroup(bd);
    if (!wgslPostprocessBG) { LOG_ERROR("Renderer") << "PostProcess bind group failed"; return; }

    // Pipeline layout
    rhi::PipelineLayoutDesc pld;
    pld.bindGroupLayouts = { wgslPostprocessLayout.get() };
    wgslPostprocessPipelineLayout = rhiBridge->createPipelineLayout(pld);
    if (!wgslPostprocessPipelineLayout) { LOG_ERROR("Renderer") << "PostProcess pipeline layout failed"; return; }

    // Pipeline: fullscreen triangle, writes directly to swapchain
    rhi::RenderPipelineDesc pd;
    pd.vertexShader   = wgslPostprocessVertexShader.get();
    pd.fragmentShader = wgslPostprocessFragmentShader.get();
    pd.layout         = wgslPostprocessPipelineLayout.get();
    pd.primitive.topology  = rhi::PrimitiveTopology::TriangleList;
    pd.primitive.cullMode  = rhi::CullMode::None;
    pd.primitive.frontFace = rhi::FrontFace::Clockwise;

    rhi::ColorTargetState ct;
    auto* sc = rhiBridge->getSwapchain();
    ct.format = sc ? sc->getFormat() : rhi::TextureFormat::BGRA8Unorm;
    ct.blend.blendEnabled = false;
    pd.colorTargets.push_back(ct);
    pd.label = "PostProcess Pipeline";

    wgslPostprocessPipeline = rhiBridge->createRenderPipeline(pd);
    if (wgslPostprocessPipeline) {
        LOG_INFO("Renderer") << "PostProcess pipeline created successfully";
    } else {
        LOG_ERROR("Renderer") << "Failed to create postprocess pipeline";
    }
}
#endif  // __EMSCRIPTEN__

// ============================================================================
// Vulkan Post-Process Pipeline (combined Tonemap + FXAA)
// ============================================================================
#ifndef __EMSCRIPTEN__

void Renderer::createPostProcessPipeline() {
    if (!rhiBridge || !rhiBridge->isReady() || !hdrColorView || !hdrSampler) {
        LOG_ERROR("Renderer") << "createPostProcessPipeline: prerequisites not ready";
        return;
    }

    auto* rhiDevice = rhiBridge->getDevice();
    auto* swapchain = rhiBridge->getSwapchain();

    // Load SPIR-V shaders (fullscreen vertex + combined tonemap+FXAA fragment)
    postprocessVertexShader = rhiBridge->createShaderFromFile(
        "shaders/tonemap.vert.spv", rhi::ShaderStage::Vertex, "main");
    postprocessFragmentShader = rhiBridge->createShaderFromFile(
        "shaders/postprocess.frag.spv", rhi::ShaderStage::Fragment, "main");

    if (!postprocessVertexShader || !postprocessFragmentShader) {
        LOG_ERROR("Renderer") << "Failed to load postprocess shaders";
        return;
    }

    // Bind group layout: 0=HDR, 1=bloom, 2=sampler, 3=SSAO
    rhi::BindGroupLayoutDesc layoutDesc;
    layoutDesc.label = "PostProcess Bind Group Layout";

    rhi::BindGroupLayoutEntry hdrEntry;
    hdrEntry.binding    = 0;
    hdrEntry.visibility = rhi::ShaderStage::Fragment;
    hdrEntry.type       = rhi::BindingType::SampledTexture;
    layoutDesc.entries.push_back(hdrEntry);

    rhi::BindGroupLayoutEntry bloomEntry;
    bloomEntry.binding    = 1;
    bloomEntry.visibility = rhi::ShaderStage::Fragment;
    bloomEntry.type       = rhi::BindingType::SampledTexture;
    layoutDesc.entries.push_back(bloomEntry);

    rhi::BindGroupLayoutEntry samplerEntry;
    samplerEntry.binding    = 2;
    samplerEntry.visibility = rhi::ShaderStage::Fragment;
    samplerEntry.type       = rhi::BindingType::Sampler;
    layoutDesc.entries.push_back(samplerEntry);

    rhi::BindGroupLayoutEntry ssaoEntry;
    ssaoEntry.binding    = 3;
    ssaoEntry.visibility = rhi::ShaderStage::Fragment;
    ssaoEntry.type       = rhi::BindingType::SampledTexture;
    layoutDesc.entries.push_back(ssaoEntry);

    postprocessBindGroupLayout = rhiDevice->createBindGroupLayout(layoutDesc);
    if (!postprocessBindGroupLayout) {
        LOG_ERROR("Renderer") << "Failed to create postprocess bind group layout";
        return;
    }

    // Build bind group (bloom/ssao may be null on first call — recreated in recreatePostProcessResources)
    auto buildPostProcessBindGroup = [&]() {
        rhi::BindGroupDesc bgDesc;
        bgDesc.layout = postprocessBindGroupLayout.get();
        bgDesc.label  = "PostProcess Bind Group";
        bgDesc.entries.push_back(rhi::BindGroupEntry::TextureView(0, hdrColorView.get()));
        // Use bloom texture if available, otherwise reuse HDR (bloom contribution will be 0)
        auto* bloomView = bloomTextureView ? bloomTextureView.get() : hdrColorView.get();
        bgDesc.entries.push_back(rhi::BindGroupEntry::TextureView(1, bloomView));
        bgDesc.entries.push_back(rhi::BindGroupEntry::Sampler(2, hdrSampler.get()));
        // SSAO texture — fall back to HDR (white = no occlusion) if not ready
        auto* ssaoView = ssaoBlurView ? ssaoBlurView.get() : hdrColorView.get();
        bgDesc.entries.push_back(rhi::BindGroupEntry::TextureView(3, ssaoView));
        postprocessBindGroup = rhiDevice->createBindGroup(bgDesc);
    };
    buildPostProcessBindGroup();

    // Pipeline layout (push constants: texelSize + bloomStrength + exposure + aoStrength)
    rhi::PipelineLayoutDesc plDesc;
    plDesc.bindGroupLayouts.push_back(postprocessBindGroupLayout.get());
    rhi::PushConstantRange pcRange;
    pcRange.stageFlags = rhi::ShaderStage::Fragment;
    pcRange.offset     = 0;
    pcRange.size       = sizeof(float) * 8;  // texelSize(2) + bloomStr + exposure + aoStr + tonemapOn + debugView + fxaaEnabled
    plDesc.pushConstantRanges.push_back(pcRange);
    postprocessPipelineLayout = rhiBridge->createPipelineLayout(plDesc);

    if (!postprocessPipelineLayout) {
        LOG_ERROR("Renderer") << "Failed to create postprocess pipeline layout";
        return;
    }

    // Render pipeline
    rhi::RenderPipelineDesc pipelineDesc;
    pipelineDesc.vertexShader   = postprocessVertexShader.get();
    pipelineDesc.fragmentShader = postprocessFragmentShader.get();
    pipelineDesc.layout         = postprocessPipelineLayout.get();
    pipelineDesc.primitive.topology = rhi::PrimitiveTopology::TriangleList;
    pipelineDesc.primitive.cullMode = rhi::CullMode::None;
    pipelineDesc.primitive.frontFace = rhi::FrontFace::Clockwise;
    pipelineDesc.label = "PostProcess Pipeline";

    rhi::ColorTargetState colorTarget;
    colorTarget.format         = swapchain ? swapchain->getFormat() : rhi::TextureFormat::BGRA8Unorm;
    colorTarget.blend.blendEnabled = false;
    pipelineDesc.colorTargets.push_back(colorTarget);

#ifdef __linux__
    auto* vulkanSwapchain = dynamic_cast<RHI::Vulkan::VulkanRHISwapchain*>(swapchain);
    if (vulkanSwapchain) {
        VkRenderPass vkPass = static_cast<VkRenderPass>(vulkanSwapchain->getPostProcessRenderPass());
        pipelineDesc.nativeRenderPass = reinterpret_cast<void*>(vkPass);
    }
#endif

    postprocessPipeline = rhiBridge->createRenderPipeline(pipelineDesc);
    if (postprocessPipeline) {
        LOG_INFO("Renderer") << "Post-process pipeline (Tonemap+FXAA) created successfully";
    } else {
        LOG_ERROR("Renderer") << "Failed to create post-process pipeline";
    }
}

// ============================================================================
// Bloom Compute Pipeline
// ============================================================================

void Renderer::createBloomPipeline() {
    if (!rhiBridge || !rhiBridge->isReady() || !hdrColorView || !hdrSampler) return;

    auto* rhiDevice = rhiBridge->getDevice();
    auto* swapchain = rhiBridge->getSwapchain();
    if (!swapchain) return;

    uint32_t bloomW = std::max(1u, swapchain->getWidth()  / 2);
    uint32_t bloomH = std::max(1u, swapchain->getHeight() / 2);

    // Create half-resolution bloom textures (storage + sampled)
    auto makeBloomTex = [&](const char* label) {
        rhi::TextureDesc desc;
        desc.size   = rhi::Extent3D(bloomW, bloomH, 1);
        desc.format = rhi::TextureFormat::RGBA16Float;
        desc.usage  = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
        desc.label  = label;
        return rhiDevice->createTexture(desc);
    };

    bloomTexture     = makeBloomTex("Bloom Texture");
    bloomPingTexture = makeBloomTex("Bloom Ping Texture");

    if (!bloomTexture || !bloomPingTexture) {
        LOG_ERROR("Renderer") << "Failed to create bloom textures";
        return;
    }

    rhi::TextureViewDesc vd;
    vd.format    = rhi::TextureFormat::RGBA16Float;
    vd.dimension = rhi::TextureViewDimension::View2D;
    bloomTextureView = bloomTexture->createView(vd);
    bloomPingView    = bloomPingTexture->createView(vd);

    rhi::SamplerDesc sd;
    sd.magFilter      = rhi::FilterMode::Linear;
    sd.minFilter      = rhi::FilterMode::Linear;
    sd.addressModeU   = rhi::AddressMode::ClampToEdge;
    sd.addressModeV   = rhi::AddressMode::ClampToEdge;
    sd.label          = "Bloom Sampler";
    bloomSampler = rhiDevice->createSampler(sd);

    // ---- Threshold pipeline ----
    bloomThresholdShader = rhiBridge->createShaderFromFile(
        "shaders/bloom_threshold.comp.spv", rhi::ShaderStage::Compute, "main");
    if (!bloomThresholdShader) {
        LOG_ERROR("Renderer") << "Failed to load bloom_threshold shader";
        return;
    }

    rhi::BindGroupLayoutDesc tLayoutDesc;
    tLayoutDesc.label = "Bloom Threshold Layout";

    auto addEntry = [&](rhi::BindGroupLayoutDesc& desc, uint32_t binding,
                        rhi::BindingType type, rhi::ShaderStage stage) {
        rhi::BindGroupLayoutEntry e;
        e.binding    = binding;
        e.visibility = stage;
        e.type       = type;
        desc.entries.push_back(e);
    };

    addEntry(tLayoutDesc, 0, rhi::BindingType::SampledTexture, rhi::ShaderStage::Compute); // hdrInput
    addEntry(tLayoutDesc, 1, rhi::BindingType::Sampler,        rhi::ShaderStage::Compute); // linearSampler
    addEntry(tLayoutDesc, 2, rhi::BindingType::StorageTexture, rhi::ShaderStage::Compute); // bloomOutput

    bloomThresholdLayout = rhiDevice->createBindGroupLayout(tLayoutDesc);

    rhi::BindGroupDesc tBgDesc;
    tBgDesc.layout = bloomThresholdLayout.get();
    tBgDesc.label  = "Bloom Threshold Bind Group";
    tBgDesc.entries.push_back(rhi::BindGroupEntry::TextureView(0, hdrColorView.get()));
    tBgDesc.entries.push_back(rhi::BindGroupEntry::Sampler(1, bloomSampler.get()));
    tBgDesc.entries.push_back(rhi::BindGroupEntry::TextureView(2, bloomTextureView.get()));
    bloomThresholdBindGroup = rhiDevice->createBindGroup(tBgDesc);

    rhi::PipelineLayoutDesc tPlDesc;
    tPlDesc.bindGroupLayouts.push_back(bloomThresholdLayout.get());
    rhi::PushConstantRange tPc;
    tPc.stageFlags = rhi::ShaderStage::Compute;
    tPc.offset     = 0;
    tPc.size       = sizeof(float) * 4;  // invInputSize (vec2) + threshold + knee
    tPlDesc.pushConstantRanges.push_back(tPc);
    bloomThresholdPipelineLayout = rhiBridge->createPipelineLayout(tPlDesc);

    rhi::ComputePipelineDesc tCpDesc;
    tCpDesc.computeShader = bloomThresholdShader.get();
    tCpDesc.layout        = bloomThresholdPipelineLayout.get();
    tCpDesc.label         = "Bloom Threshold Pipeline";
    bloomThresholdPipeline = rhiDevice->createComputePipeline(tCpDesc);

    // ---- Blur pipeline ----
    bloomBlurShader = rhiBridge->createShaderFromFile(
        "shaders/bloom_blur.comp.spv", rhi::ShaderStage::Compute, "main");
    if (!bloomBlurShader) {
        LOG_ERROR("Renderer") << "Failed to load bloom_blur shader";
        return;
    }

    rhi::BindGroupLayoutDesc bLayoutDesc;
    bLayoutDesc.label = "Bloom Blur Layout";
    addEntry(bLayoutDesc, 0, rhi::BindingType::SampledTexture, rhi::ShaderStage::Compute); // bloomInput
    addEntry(bLayoutDesc, 1, rhi::BindingType::Sampler,        rhi::ShaderStage::Compute); // linearSampler
    addEntry(bLayoutDesc, 2, rhi::BindingType::StorageTexture, rhi::ShaderStage::Compute); // bloomOutput

    bloomBlurLayout = rhiDevice->createBindGroupLayout(bLayoutDesc);

    // ping→pong bind group (blur iteration 0, 2)
    {
        rhi::BindGroupDesc bd;
        bd.layout = bloomBlurLayout.get();
        bd.label  = "Bloom Blur BG ping→pong";
        bd.entries.push_back(rhi::BindGroupEntry::TextureView(0, bloomTextureView.get()));
        bd.entries.push_back(rhi::BindGroupEntry::Sampler(1, bloomSampler.get()));
        bd.entries.push_back(rhi::BindGroupEntry::TextureView(2, bloomPingView.get()));
        bloomBlurBindGroups[0] = rhiDevice->createBindGroup(bd);
    }
    // pong→ping bind group (blur iteration 1)
    {
        rhi::BindGroupDesc bd;
        bd.layout = bloomBlurLayout.get();
        bd.label  = "Bloom Blur BG pong→ping";
        bd.entries.push_back(rhi::BindGroupEntry::TextureView(0, bloomPingView.get()));
        bd.entries.push_back(rhi::BindGroupEntry::Sampler(1, bloomSampler.get()));
        bd.entries.push_back(rhi::BindGroupEntry::TextureView(2, bloomTextureView.get()));
        bloomBlurBindGroups[1] = rhiDevice->createBindGroup(bd);
    }

    rhi::PipelineLayoutDesc bPlDesc;
    bPlDesc.bindGroupLayouts.push_back(bloomBlurLayout.get());
    rhi::PushConstantRange bPc;
    bPc.stageFlags = rhi::ShaderStage::Compute;
    bPc.offset     = 0;
    bPc.size       = sizeof(float) * 4;  // invInputSize (vec2) + iteration (int) + pad
    bPlDesc.pushConstantRanges.push_back(bPc);
    bloomBlurPipelineLayout = rhiBridge->createPipelineLayout(bPlDesc);

    rhi::ComputePipelineDesc bCpDesc;
    bCpDesc.computeShader = bloomBlurShader.get();
    bCpDesc.layout        = bloomBlurPipelineLayout.get();
    bCpDesc.label         = "Bloom Blur Pipeline";
    bloomBlurPipeline = rhiDevice->createComputePipeline(bCpDesc);

    if (bloomThresholdPipeline && bloomBlurPipeline) {
        LOG_INFO("Renderer") << "Bloom pipeline created (" << bloomW << "x" << bloomH << ")";
    } else {
        LOG_ERROR("Renderer") << "Failed to create bloom pipelines";
    }
}

// ============================================================================
// SSAO Compute Pipelines (screen-space ambient occlusion + bilateral blur)
// ============================================================================

void Renderer::createSSAOPipeline() {
    if (!rhiBridge || !rhiBridge->isReady() || !rhiDepthImageView) return;

    auto* rhiDevice = rhiBridge->getDevice();
    auto* swapchain = rhiBridge->getSwapchain();
    if (!swapchain) return;

    uint32_t W = swapchain->getWidth();
    uint32_t H = swapchain->getHeight();
    // Run SSAO at half resolution (4× fewer pixels = 4× faster on lavapipe)
    // Bilinear upsampling in postprocess.frag is transparent since sampler is Linear
    uint32_t ssaoW = std::max(1u, W / 2);
    uint32_t ssaoH = std::max(1u, H / 2);

    // Create half-resolution R8Unorm SSAO textures (raw + blurred)
    auto makeSSAOTex = [&](const char* label) {
        rhi::TextureDesc desc;
        desc.label  = label;
        desc.format = rhi::TextureFormat::R8Unorm;
        desc.size   = rhi::Extent3D(ssaoW, ssaoH, 1);
        desc.usage  = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
        return rhiDevice->createTexture(desc);
    };

    ssaoTexture     = makeSSAOTex("SSAO Texture");
    ssaoBlurTexture = makeSSAOTex("SSAO Blur Texture");

    if (!ssaoTexture || !ssaoBlurTexture) {
        LOG_ERROR("Renderer") << "Failed to create SSAO textures";
        return;
    }

    rhi::TextureViewDesc vd;
    vd.format    = rhi::TextureFormat::R8Unorm;
    vd.dimension = rhi::TextureViewDimension::View2D;
    ssaoTextureView = ssaoTexture->createView(vd);
    ssaoBlurView    = ssaoBlurTexture->createView(vd);

    // Sampler for reading SSAO textures — use Nearest: R8Unorm on lavapipe
    // does not support VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT
    rhi::SamplerDesc sd;
    sd.minFilter = sd.magFilter = rhi::FilterMode::Nearest;
    sd.addressModeU = sd.addressModeV = sd.addressModeW = rhi::AddressMode::ClampToEdge;
    ssaoSampler = rhiDevice->createSampler(sd);

    // ---- SSAO compute pipeline ----
    ssaoShader = rhiBridge->createShaderFromFile("shaders/ssao.comp.spv", rhi::ShaderStage::Compute, "main");
    if (!ssaoShader) { LOG_ERROR("Renderer") << "Failed to load ssao.comp.spv"; return; }

    // Layout: binding 0 = depth (sampled), binding 1 = AO output (storage)
    rhi::BindGroupLayoutDesc ssaoLayoutDesc;
    ssaoLayoutDesc.label = "SSAO Bind Group Layout";
    {
        rhi::BindGroupLayoutEntry e;
        e.binding = 0; e.visibility = rhi::ShaderStage::Compute;
        e.type = rhi::BindingType::SampledTexture;
        ssaoLayoutDesc.entries.push_back(e);
    }
    {
        rhi::BindGroupLayoutEntry e;
        e.binding = 1; e.visibility = rhi::ShaderStage::Compute;
        e.type = rhi::BindingType::StorageTexture;
        ssaoLayoutDesc.entries.push_back(e);
    }
    {
        rhi::BindGroupLayoutEntry e;
        e.binding = 2; e.visibility = rhi::ShaderStage::Compute;
        e.type = rhi::BindingType::Sampler;
        ssaoLayoutDesc.entries.push_back(e);
    }
    ssaoLayout = rhiDevice->createBindGroupLayout(ssaoLayoutDesc);

    {
        rhi::BindGroupDesc bd;
        bd.layout = ssaoLayout.get();
        bd.label  = "SSAO Bind Group";
        bd.entries.push_back(rhi::BindGroupEntry::TextureView(0, rhiDepthImageView.get()));
        bd.entries.push_back(rhi::BindGroupEntry::TextureView(1, ssaoTextureView.get()));
        bd.entries.push_back(rhi::BindGroupEntry::Sampler(2, ssaoSampler.get()));
        ssaoBindGroup = rhiDevice->createBindGroup(bd);
    }

    rhi::PipelineLayoutDesc ssaoPLDesc;
    ssaoPLDesc.bindGroupLayouts.push_back(ssaoLayout.get());
    {
        rhi::PushConstantRange pc;
        pc.stageFlags = rhi::ShaderStage::Compute;
        pc.offset = 0;
        // mat4(64) + radius+bias+near+far+invW+invH(24) + pad(8) = 96
        pc.size = 96;
        ssaoPLDesc.pushConstantRanges.push_back(pc);
    }
    ssaoPipelineLayout = rhiBridge->createPipelineLayout(ssaoPLDesc);

    {
        rhi::ComputePipelineDesc cpDesc;
        cpDesc.computeShader = ssaoShader.get();
        cpDesc.layout        = ssaoPipelineLayout.get();
        ssaoPipeline = rhiDevice->createComputePipeline(cpDesc);
    }

    // ---- SSAO blur compute pipeline ----
    ssaoBlurShader = rhiBridge->createShaderFromFile("shaders/ssao_blur.comp.spv", rhi::ShaderStage::Compute, "main");
    if (!ssaoBlurShader) { LOG_ERROR("Renderer") << "Failed to load ssao_blur.comp.spv"; return; }

    // Layout: binding 0 = raw SSAO (sampled), binding 1 = depth (sampled), binding 2 = blurred (storage)
    rhi::BindGroupLayoutDesc ssaoBlurLayoutDesc;
    ssaoBlurLayoutDesc.label = "SSAO Blur Bind Group Layout";
    {
        rhi::BindGroupLayoutEntry e;
        e.binding = 0; e.visibility = rhi::ShaderStage::Compute;
        e.type = rhi::BindingType::SampledTexture;
        ssaoBlurLayoutDesc.entries.push_back(e);
    }
    {
        rhi::BindGroupLayoutEntry e;
        e.binding = 1; e.visibility = rhi::ShaderStage::Compute;
        e.type = rhi::BindingType::SampledTexture;
        ssaoBlurLayoutDesc.entries.push_back(e);
    }
    {
        rhi::BindGroupLayoutEntry e;
        e.binding = 2; e.visibility = rhi::ShaderStage::Compute;
        e.type = rhi::BindingType::StorageTexture;
        ssaoBlurLayoutDesc.entries.push_back(e);
    }
    {
        rhi::BindGroupLayoutEntry e;
        e.binding = 3; e.visibility = rhi::ShaderStage::Compute;
        e.type = rhi::BindingType::Sampler;
        ssaoBlurLayoutDesc.entries.push_back(e);
    }
    ssaoBlurLayout = rhiDevice->createBindGroupLayout(ssaoBlurLayoutDesc);

    {
        rhi::BindGroupDesc bd;
        bd.layout = ssaoBlurLayout.get();
        bd.label  = "SSAO Blur Bind Group";
        bd.entries.push_back(rhi::BindGroupEntry::TextureView(0, ssaoTextureView.get()));
        bd.entries.push_back(rhi::BindGroupEntry::TextureView(1, rhiDepthImageView.get()));
        bd.entries.push_back(rhi::BindGroupEntry::TextureView(2, ssaoBlurView.get()));
        bd.entries.push_back(rhi::BindGroupEntry::Sampler(3, ssaoSampler.get()));
        ssaoBlurBindGroup = rhiDevice->createBindGroup(bd);
    }

    rhi::PipelineLayoutDesc blurPLDesc;
    blurPLDesc.bindGroupLayouts.push_back(ssaoBlurLayout.get());
    {
        rhi::PushConstantRange pc;
        pc.stageFlags = rhi::ShaderStage::Compute;
        pc.offset = 0;
        pc.size = 24;  // invW+invH+near+far+depthThreshold+pad
        blurPLDesc.pushConstantRanges.push_back(pc);
    }
    ssaoBlurPipelineLayout = rhiBridge->createPipelineLayout(blurPLDesc);

    {
        rhi::ComputePipelineDesc cpDesc;
        cpDesc.computeShader = ssaoBlurShader.get();
        cpDesc.layout        = ssaoBlurPipelineLayout.get();
        ssaoBlurPipeline = rhiDevice->createComputePipeline(cpDesc);
    }

    if (ssaoPipeline && ssaoBlurPipeline) {
        LOG_INFO("Renderer") << "SSAO pipeline created (" << ssaoW << "x" << ssaoH << ")";
    } else {
        LOG_ERROR("Renderer") << "Failed to create SSAO pipelines";
    }
}

void Renderer::recreatePostProcessResources() {
    auto* swapchain = rhiBridge->getSwapchain();
    if (!swapchain || !hdrColorView) return;

    auto* rhiDevice = rhiBridge->getDevice();

#ifdef __linux__
    auto* vulkanSwapchain = dynamic_cast<RHI::Vulkan::VulkanRHISwapchain*>(swapchain);
    if (vulkanSwapchain && rhiDepthImageView) {
        auto* hdrVkView   = dynamic_cast<RHI::Vulkan::VulkanRHITextureView*>(hdrColorView.get());
        auto* depthVkView = dynamic_cast<RHI::Vulkan::VulkanRHITextureView*>(rhiDepthImageView.get());
        if (hdrVkView && depthVkView) {
            vulkanSwapchain->createHDRRenderPass();
            vulkanSwapchain->createHDRLoadRenderPass();   // load variant for DeferredLighting
            vulkanSwapchain->createHDRFramebuffer(hdrVkView->getVkImageView(),
                                                  depthVkView->getVkImageView());
            vulkanSwapchain->createPostProcessRenderPass();
            vulkanSwapchain->createPostProcessFramebuffers();
        }
    }
#endif

    // Recreate bloom textures at new size
    createBloomPipeline();

    // Recreate SSAO textures at new size
    createSSAOPipeline();

    // Rebuild post-process bind group with updated bloom + SSAO texture views
    if (postprocessBindGroupLayout && hdrColorView && hdrSampler) {
        rhi::BindGroupDesc bgDesc;
        bgDesc.layout = postprocessBindGroupLayout.get();
        bgDesc.label  = "PostProcess Bind Group";
        bgDesc.entries.push_back(rhi::BindGroupEntry::TextureView(0, hdrColorView.get()));
        auto* bloomView = bloomTextureView ? bloomTextureView.get() : hdrColorView.get();
        bgDesc.entries.push_back(rhi::BindGroupEntry::TextureView(1, bloomView));
        bgDesc.entries.push_back(rhi::BindGroupEntry::Sampler(2, hdrSampler.get()));
        auto* ssaoView = ssaoBlurView ? ssaoBlurView.get() : hdrColorView.get();
        bgDesc.entries.push_back(rhi::BindGroupEntry::TextureView(3, ssaoView));
        postprocessBindGroup = rhiDevice->createBindGroup(bgDesc);
    }
}

#endif  // !__EMSCRIPTEN__

void Renderer::performFrustumCulling(rhi::RHICommandEncoder* encoder, uint32_t frameIndex,
                                     uint32_t objectCount, uint32_t indexCount) {
    if (!cullPipeline || objectCount == 0) return;

    auto* objectBuffer = pendingInstancedData->objectBuffer;

    // Step 1: Write CullUBO (frustum planes, objectCount, indexCount)
    CullUBO cullUbo;
    glm::mat4 vp = projectionMatrix * viewMatrix;
    extractFrustumPlanes(vp, cullUbo.frustumPlanes);
    cullUbo.objectCount = objectCount;
    cullUbo.indexCount = indexCount;
    cullUbo.pad[0] = 0;
    cullUbo.pad[1] = 0;
    cullUniformBuffers[frameIndex]->write(&cullUbo, sizeof(CullUBO));

    // Step 2: Reset indirect draw buffer (instanceCount = 0, indexCount = mesh indexCount)
    struct DrawIndexedIndirectCommand {
        uint32_t indexCount;
        uint32_t instanceCount;
        uint32_t firstIndex;
        int32_t  vertexOffset;
        uint32_t firstInstance;
    };
    DrawIndexedIndirectCommand cmd;
    cmd.indexCount = indexCount;
    cmd.instanceCount = 0;  // Will be filled by compute shader
    cmd.firstIndex = 0;
    cmd.vertexOffset = 0;
    cmd.firstInstance = 0;
    indirectDrawBuffers[frameIndex]->write(&cmd, sizeof(cmd));

#ifndef __EMSCRIPTEN__
    // Step 3: Vulkan barriers — host writes visible to compute shader
    auto* vulkanEncoder = dynamic_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(encoder);
    if (vulkanEncoder) {
        auto& cmdBuf = vulkanEncoder->getCommandBuffer();

        // Barrier for CullUBO
        auto* vulkanCullUbo = dynamic_cast<RHI::Vulkan::VulkanRHIBuffer*>(cullUniformBuffers[frameIndex].get());
        auto* vulkanIndirect = dynamic_cast<RHI::Vulkan::VulkanRHIBuffer*>(indirectDrawBuffers[frameIndex].get());
        auto* vulkanObjectBuf = dynamic_cast<RHI::Vulkan::VulkanRHIBuffer*>(objectBuffer);

        std::vector<vk::BufferMemoryBarrier> barriers;
        if (vulkanCullUbo) {
            barriers.push_back(vk::BufferMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eHostWrite,
                .dstAccessMask = vk::AccessFlagBits::eUniformRead,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = vulkanCullUbo->getVkBuffer(),
                .offset = 0,
                .size = VK_WHOLE_SIZE
            });
        }
        if (vulkanIndirect) {
            barriers.push_back(vk::BufferMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eHostWrite,
                .dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = vulkanIndirect->getVkBuffer(),
                .offset = 0,
                .size = VK_WHOLE_SIZE
            });
        }
        if (vulkanObjectBuf) {
            barriers.push_back(vk::BufferMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eHostWrite,
                .dstAccessMask = vk::AccessFlagBits::eShaderRead,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = vulkanObjectBuf->getVkBuffer(),
                .offset = 0,
                .size = VK_WHOLE_SIZE
            });
        }

        if (!barriers.empty()) {
            cmdBuf.pipelineBarrier(
                vk::PipelineStageFlagBits::eHost,
                vk::PipelineStageFlagBits::eComputeShader,
                {}, {}, barriers, {}
            );
        }
    }
#endif

    // Step 4: Create/update cull bind group if objectBuffer changed
    if (objectBuffer != cachedObjectBuffers[frameIndex] || !cullBindGroups[frameIndex]) {
        rhi::BindGroupDesc cullBgDesc;
        cullBgDesc.layout = cullBindGroupLayout.get();
        cullBgDesc.entries.push_back(rhi::BindGroupEntry::Buffer(0, cullUniformBuffers[frameIndex].get()));
        cullBgDesc.entries.push_back(rhi::BindGroupEntry::Buffer(1, objectBuffer));
        cullBgDesc.entries.push_back(rhi::BindGroupEntry::Buffer(2, indirectDrawBuffers[frameIndex].get()));
        cullBgDesc.entries.push_back(rhi::BindGroupEntry::Buffer(3, visibleIndicesBuffers[frameIndex].get()));
        cullBgDesc.label = "Cull Bind Group";
        cullBindGroups[frameIndex] = rhiBridge->getDevice()->createBindGroup(cullBgDesc);
    }

    // Step 5: Dispatch compute shader
    auto computePass = encoder->beginComputePass("Frustum_Cull");
    computePass->setPipeline(cullPipeline.get());
    computePass->setBindGroup(0, cullBindGroups[frameIndex].get());
    computePass->dispatch((objectCount + 63) / 64, 1, 1);
    computePass->end();

#ifndef __EMSCRIPTEN__
    // Step 6: Post-compute barriers — compute writes visible to vertex shader + indirect draw
    if (vulkanEncoder) {
        auto& cmdBuf = vulkanEncoder->getCommandBuffer();
        auto* vulkanIndirect = dynamic_cast<RHI::Vulkan::VulkanRHIBuffer*>(indirectDrawBuffers[frameIndex].get());
        auto* vulkanVisibleIndices = dynamic_cast<RHI::Vulkan::VulkanRHIBuffer*>(visibleIndicesBuffers[frameIndex].get());

        std::vector<vk::BufferMemoryBarrier> postBarriers;
        if (vulkanIndirect) {
            postBarriers.push_back(vk::BufferMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eShaderWrite,
                .dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = vulkanIndirect->getVkBuffer(),
                .offset = 0,
                .size = VK_WHOLE_SIZE
            });
        }
        if (vulkanVisibleIndices) {
            postBarriers.push_back(vk::BufferMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eShaderWrite,
                .dstAccessMask = vk::AccessFlagBits::eShaderRead,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = vulkanVisibleIndices->getVkBuffer(),
                .offset = 0,
                .size = VK_WHOLE_SIZE
            });
        }

        if (!postBarriers.empty()) {
            cmdBuf.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eDrawIndirect | vk::PipelineStageFlagBits::eVertexShader,
                {}, {}, postBarriers, {}
            );
        }
    }
#endif
}

void Renderer::performFrustumCullingAsync(uint32_t frameIndex, uint32_t objectCount, uint32_t indexCount) {
    if (!cullPipeline || objectCount == 0 || !useAsyncCompute) return;

    auto* device = rhiBridge->getDevice();
    auto* objectBuffer = pendingInstancedData->objectBuffer;

    // Step 1: Write CullUBO
    CullUBO cullUbo;
    glm::mat4 vp = projectionMatrix * viewMatrix;
    extractFrustumPlanes(vp, cullUbo.frustumPlanes);
    cullUbo.objectCount = objectCount;
    cullUbo.indexCount = indexCount;
    cullUbo.pad[0] = 0;
    cullUbo.pad[1] = 0;
    cullUniformBuffers[frameIndex]->write(&cullUbo, sizeof(CullUBO));

    // Step 2: Reset indirect draw buffer
    struct DrawIndexedIndirectCommand {
        uint32_t indexCount;
        uint32_t instanceCount;
        uint32_t firstIndex;
        int32_t  vertexOffset;
        uint32_t firstInstance;
    };
    DrawIndexedIndirectCommand cmd;
    cmd.indexCount = indexCount;
    cmd.instanceCount = 0;
    cmd.firstIndex = 0;
    cmd.vertexOffset = 0;
    cmd.firstInstance = 0;
    indirectDrawBuffers[frameIndex]->write(&cmd, sizeof(cmd));

    // Step 3: Create/update cull bind group
    if (objectBuffer != cachedObjectBuffers[frameIndex] || !cullBindGroups[frameIndex]) {
        rhi::BindGroupDesc cullBgDesc;
        cullBgDesc.layout = cullBindGroupLayout.get();
        cullBgDesc.entries.push_back(rhi::BindGroupEntry::Buffer(0, cullUniformBuffers[frameIndex].get()));
        cullBgDesc.entries.push_back(rhi::BindGroupEntry::Buffer(1, objectBuffer));
        cullBgDesc.entries.push_back(rhi::BindGroupEntry::Buffer(2, indirectDrawBuffers[frameIndex].get()));
        cullBgDesc.entries.push_back(rhi::BindGroupEntry::Buffer(3, visibleIndicesBuffers[frameIndex].get()));
        cullBgDesc.label = "Cull Bind Group";
        cullBindGroups[frameIndex] = device->createBindGroup(cullBgDesc);
    }

    // Step 4: Create compute command encoder from compute pool
    auto computeEncoder = device->createCommandEncoder(rhi::QueueType::Compute);
    if (!computeEncoder) return;

#ifndef __EMSCRIPTEN__
    // Pre-compute barriers: host writes visible to compute shader
    auto* vulkanEncoder = dynamic_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(computeEncoder.get());
    if (vulkanEncoder) {
        auto& cmdBuf = vulkanEncoder->getCommandBuffer();
        auto* vulkanCullUbo = dynamic_cast<RHI::Vulkan::VulkanRHIBuffer*>(cullUniformBuffers[frameIndex].get());
        auto* vulkanIndirect = dynamic_cast<RHI::Vulkan::VulkanRHIBuffer*>(indirectDrawBuffers[frameIndex].get());
        auto* vulkanObjectBuf = dynamic_cast<RHI::Vulkan::VulkanRHIBuffer*>(objectBuffer);

        std::vector<vk::BufferMemoryBarrier> barriers;
        if (vulkanCullUbo) {
            barriers.push_back(vk::BufferMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eHostWrite,
                .dstAccessMask = vk::AccessFlagBits::eUniformRead,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = vulkanCullUbo->getVkBuffer(),
                .offset = 0, .size = VK_WHOLE_SIZE
            });
        }
        if (vulkanIndirect) {
            barriers.push_back(vk::BufferMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eHostWrite,
                .dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = vulkanIndirect->getVkBuffer(),
                .offset = 0, .size = VK_WHOLE_SIZE
            });
        }
        if (vulkanObjectBuf) {
            barriers.push_back(vk::BufferMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eHostWrite,
                .dstAccessMask = vk::AccessFlagBits::eShaderRead,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = vulkanObjectBuf->getVkBuffer(),
                .offset = 0, .size = VK_WHOLE_SIZE
            });
        }
        if (!barriers.empty()) {
            cmdBuf.pipelineBarrier(
                vk::PipelineStageFlagBits::eHost,
                vk::PipelineStageFlagBits::eComputeShader,
                {}, {}, barriers, {}
            );
        }
    }
#endif

    // Step 5: Dispatch compute shader
    auto computePass = computeEncoder->beginComputePass("Async_Frustum_Cull");
    computePass->setPipeline(cullPipeline.get());
    computePass->setBindGroup(0, cullBindGroups[frameIndex].get());
    computePass->dispatch((objectCount + 63) / 64, 1, 1);
    computePass->end();

    // No post-compute barriers needed — concurrent sharing mode handles visibility
    // Timeline semaphore provides execution ordering

    // Step 6: Submit to compute queue with timeline signal
    auto computeCmdBuffer = computeEncoder->finish();
    if (computeCmdBuffer) {
        rhi::SubmitInfo computeSubmit;
        computeSubmit.commandBuffers.push_back(computeCmdBuffer.get());
        computeSubmit.timelineSignals.push_back(
            rhi::TimelineSignal{computeTimelineSemaphore.get(), ++computeTimelineValue});

        auto* computeQueue = device->getQueue(rhi::QueueType::Compute);
        computeQueue->submit(computeSubmit);
    }
}

// ============================================================================
// Phase 8: RHI Uniform Buffer Update
// ============================================================================

void Renderer::updateRHIUniformBuffer(uint32_t currentImage) {
    if (currentImage >= rhiUniformBuffers.size() || !rhiUniformBuffers[currentImage]) {
        return;
    }

    UniformBufferObject ubo{};
    ubo.model   = glm::mat4(1.0f);
    ubo.view    = viewMatrix;
    ubo.proj    = projectionMatrix;
    ubo.invView = glm::inverse(viewMatrix);
    ubo.invProj = glm::inverse(projectionMatrix);

    ubo.sunDirection     = sunDirection;
    ubo.sunIntensity     = sunIntensity;
    ubo.sunColor         = sunColor;
    ubo.ambientIntensity = ambientIntensity;
    ubo.cameraPos        = cameraPosition;
    ubo.exposure         = exposure;

    // Phase 3: CSM shadow parameters
    if (shadowRenderer && shadowRenderer->isInitialized()) {
        for (uint32_t i = 0; i < rendering::ShadowRenderer::NUM_CASCADES; ++i)
            ubo.lightSpaceMatrices[i] = shadowRenderer->getLightSpaceMatrix(i);
        ubo.cascadeSplits = shadowRenderer->getCascadeSplits();
    } else {
        for (auto& m : ubo.lightSpaceMatrices) m = glm::mat4(1.0f);
        ubo.cascadeSplits = glm::vec4(10.f, 50.f, 200.f, 1000.f);
    }
    ubo.shadowMapSize  = glm::vec2(rendering::ShadowRenderer::SHADOW_MAP_SIZE);
    ubo.shadowBias     = shadowBias;
    ubo.shadowStrength = shadowStrength;

    // Phase 4 showcase: pack dynamic point lights
    ubo.numPointLights = static_cast<uint32_t>(
        std::min(pendingPointLights.size(), static_cast<size_t>(MAX_POINT_LIGHTS)));
    for (uint32_t i = 0; i < ubo.numPointLights; ++i)
        ubo.pointLights[i] = pendingPointLights[i];
    ubo.debugCascades = debugCascades ? 1.0f : 0.0f;
    ubo.debugView     = debugView;
    ubo.abSplitX      = abSplitX;

    // Copy to RHI uniform buffer - always use write() to ensure proper flush to GPU
    auto* buffer = rhiUniformBuffers[currentImage].get();
    if (buffer) {
        buffer->write(&ubo, sizeof(ubo));
    }
}

// ============================================================================
// Phase 7: Primary RHI Render Loop (migrated from drawFrameRHI)
// ============================================================================

void Renderer::drawFrame() {
    // Complete RHI rendering path using RHI abstractions
    // Phase 7: Replaces legacy Vulkan rendering (now drawFrameLegacy)

#ifdef __EMSCRIPTEN__
    // Tell JS we are inside wasm-side work, including the fence-wait
    // emscripten_sleep windows. JS callbacks (setInterval timing poll, etc.)
    // gate their wasm calls on Module._wasmBusy to avoid the ASYNCIFY
    // "multiple async operations in flight" assertion. RAII guard so every
    // early-return path resets the flag cleanly.
    struct BusyFlagGuard {
        BusyFlagGuard()  { EM_ASM({ Module._wasmBusy = true;  }); }
        ~BusyFlagGuard() { EM_ASM({ Module._wasmBusy = false; }); }
    };
    BusyFlagGuard _busyGuard;
#endif

    if (!rhiBridge || !rhiBridge->isReady()) {
        return;
    }

    // Initialize swapchain if not already done
    if (!rhiBridge->getSwapchain()) {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        rhiBridge->createSwapchain(static_cast<uint32_t>(width), static_cast<uint32_t>(height), true);
    }

    // Ensure render resources (framebuffers) are ready before rendering
    auto* swapchain = rhiBridge->getSwapchain();
    if (swapchain && rhiDepthImageView) {
        swapchain->ensureRenderResourcesReady(rhiDepthImageView.get());
    }

    // Step 1: Begin frame (wait for fence, acquire swapchain image)
    if (!rhiBridge->beginFrame()) {
        // Swapchain needs recreation - skip this frame
        return;
    }

    uint32_t frameIndex = rhiBridge->getCurrentFrameIndex();

    // Phase 4: Reset per-frame descriptor pool for this frame slot.
    // Currently no dynamic bind groups are allocated from it (infrastructure only),
    // so this is a no-op that primes the pool for future per-frame allocations.
#ifndef __EMSCRIPTEN__
    {
        auto* vd = dynamic_cast<RHI::Vulkan::VulkanRHIDevice*>(rhiBridge->getDevice());
        if (vd) vd->resetPerFrameDescriptorPool(frameIndex);
    }
#endif

    // Step 2: Update CSM light matrices (before uniform buffer update)
    if (shadowRenderer && shadowRenderer->isInitialized()) {
        // The scene is centered near the origin; the camera orbits it at
        // ~|cameraPosition|. Passing 0.1 / sceneRadius as the cascade range
        // wasted cascades 0-2 on empty space in front of the scene and left
        // the far half of the scene outside every cascade. Span the cascades
        // over the scene's actual view-depth range instead.
        float camDist = glm::length(cameraPosition);
        float sNear   = std::max(0.1f, camDist - shadowSceneRadius);
        float sFar    = camDist + shadowSceneRadius;
        shadowRenderer->updateLightMatrices(sunDirection, cameraPosition,
                                            viewMatrix, projectionMatrix,
                                            sNear, sFar, shadowSceneRadius);
    }

    // Step 3: Update uniform buffer with RHI (includes shadow matrix)
    updateRHIUniformBuffer(frameIndex);

    // Step 4: Create command encoder
    auto encoder = rhiBridge->createCommandEncoder();
    if (!encoder) {
        return;
    }

    // Phase 4.1: GPU Profiling — begin frame (read back previous results, reset query pool)
#ifndef __EMSCRIPTEN__
    if (gpuProfiler) {
        auto* vulkanEncoder = dynamic_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(encoder.get());
        if (vulkanEncoder) {
            gpuProfiler->beginFrame(vulkanEncoder->getCommandBuffer(), frameIndex);
        }
    }
#endif

    // Windows: emit UNDEFINED→ShaderReadOnly barriers for IBL textures + dummy skybox cubemap
    // once on the first frame that uses IBL. The validation layer on Windows doesn't track
    // layout transitions from one-time-submit init command buffers into the frame's cmd buffer.
#if !defined(__linux__) && !defined(__EMSCRIPTEN__)
    if (!m_iblBarriersEmitted) {
        if (iblManager && iblManager->isInitialized())
            iblManager->emitInitializationBarriers(encoder.get());
        if (skyboxRenderer && !skyboxRenderer->hasEnvironmentMap()
                && skyboxRenderer->getDummyEnvTexture()) {
            encoder->transitionTextureLayout(
                skyboxRenderer->getDummyEnvTexture(),
                rhi::TextureLayout::Undefined,
                rhi::TextureLayout::ShaderReadOnly);
        }
        m_iblBarriersEmitted = true;
    }
#endif

    // Step 5: SSBO setup + frustum culling + shadow pass
    if (pendingInstancedData && pendingInstancedData->instanceCount > 0) {
        auto* mesh = pendingInstancedData->mesh;
        auto* objectBuffer = pendingInstancedData->objectBuffer;

        if (mesh && mesh->hasData() && objectBuffer) {
            // Phase 2.1+2.2: Create/update SSBO bind group if buffer changed
            if (objectBuffer != cachedObjectBuffers[frameIndex]) {
                rhi::BindGroupDesc ssboDesc;
                ssboDesc.layout = ssboBindGroupLayout.get();
                ssboDesc.entries.push_back(rhi::BindGroupEntry::Buffer(0, objectBuffer));
                ssboDesc.entries.push_back(rhi::BindGroupEntry::Buffer(1, visibleIndicesBuffers[frameIndex].get()));
                ssboDesc.label = "SSBO Bind Group";
                ssboBindGroups[frameIndex] = rhiBridge->getDevice()->createBindGroup(ssboDesc);
                cachedObjectBuffers[frameIndex] = objectBuffer;

                // Also invalidate cull bind group since objectBuffer changed
                cullBindGroups[frameIndex].reset();
            }

            // Phase 2.2+3.2: Perform GPU frustum culling
            uint32_t instanceCount = pendingInstancedData->instanceCount;
            lastInstanceCount = instanceCount;  // cache for metrics panel
            uint32_t meshIndexCount = static_cast<uint32_t>(mesh->getIndexCount());

#ifndef __EMSCRIPTEN__
            // GPU Profiling: begin frustum culling timer
            if (gpuProfiler && !useAsyncCompute) {
                auto* ve = dynamic_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(encoder.get());
                if (ve) gpuProfiler->beginTimer(ve->getCommandBuffer(), frameIndex,
                    GpuProfiler::TimerId::FrustumCulling, vk::PipelineStageFlagBits::eComputeShader);
            }
#endif

            if (useAsyncCompute) {
                // Async: separate compute encoder submitted to compute queue
                performFrustumCullingAsync(frameIndex, instanceCount, meshIndexCount);
            } else {
                // Inline: compute on graphics queue command buffer
                performFrustumCulling(encoder.get(), frameIndex, instanceCount, meshIndexCount);
            }

#ifndef __EMSCRIPTEN__
            // GPU Profiling: end frustum culling timer
            if (gpuProfiler && !useAsyncCompute) {
                auto* ve = dynamic_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(encoder.get());
                if (ve) gpuProfiler->endTimer(ve->getCommandBuffer(), frameIndex,
                    GpuProfiler::TimerId::FrustumCulling, vk::PipelineStageFlagBits::eComputeShader);
            }
#endif

            // Shadow pass (render scene from light's perspective — uses direct drawIndexed, no culling)
#ifndef __EMSCRIPTEN__
            if (gpuProfiler) {
                auto* ve = dynamic_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(encoder.get());
                if (ve) gpuProfiler->beginTimer(ve->getCommandBuffer(), frameIndex,
                    GpuProfiler::TimerId::ShadowPass);
            }
#endif
            // Phase 3: CSM — 4 cascade shadow passes
            if (shadowRenderer && shadowRenderer->isInitialized() && instanceCount > 1) {
#if !defined(__EMSCRIPTEN__) && !defined(__linux__)
                // macOS/Windows: Transition shadow array to depth attachment (all 4 layers)
                auto* vulkanEncoder = dynamic_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(encoder.get());
                auto* shadowTexture = dynamic_cast<RHI::Vulkan::VulkanRHITexture*>(shadowRenderer->getShadowMapTexture());
                if (vulkanEncoder && shadowTexture) {
                    vulkanEncoder->getCommandBuffer().pipelineBarrier(
                        vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eEarlyFragmentTests,
                        vk::PipelineStageFlagBits::eEarlyFragmentTests,
                        {}, {}, {},
                        vk::ImageMemoryBarrier{
                            .srcAccessMask       = vk::AccessFlagBits::eShaderRead,
                            .dstAccessMask       = vk::AccessFlagBits::eDepthStencilAttachmentWrite,
                            .oldLayout           = vk::ImageLayout::eUndefined,
                            .newLayout           = vk::ImageLayout::eDepthStencilAttachmentOptimal,
                            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                            .image               = shadowTexture->getVkImage(),
                            .subresourceRange    = vk::ImageSubresourceRange{
                                vk::ImageAspectFlagBits::eDepth, 0, 1, 0,
                                rendering::ShadowRenderer::NUM_CASCADES
                            }
                        }
                    );
                }
#endif

                for (uint32_t cascade = 0; cascade < rendering::ShadowRenderer::NUM_CASCADES; ++cascade) {
                    auto* shadowPass = shadowRenderer->beginShadowPass(encoder.get(), frameIndex, cascade);
                    if (shadowPass) {
                        if (ssboBindGroups[frameIndex])
                            shadowPass->setBindGroup(1, ssboBindGroups[frameIndex].get());
                        shadowPass->setVertexBuffer(0, mesh->getVertexBuffer(), 0);
                        shadowPass->setIndexBuffer(mesh->getIndexBuffer(), rhi::IndexFormat::Uint32, 0);
                        // Render ALL instances with trivial 0-based indexing;
                        // shadow.wgsl culls the ground by its huge AABB. This
                        // avoids the Vulkan↔WebGPU instance_index/firstInstance
                        // discrepancy that let the ground into the shadow map.
                        shadowPass->drawIndexed(meshIndexCount, instanceCount, 0, 0, 0);
                        shadowRenderer->endShadowPass();
                    }
                }

#if !defined(__EMSCRIPTEN__) && !defined(__linux__)
                // macOS/Windows: Transition shadow array to shader read
                auto* vulkanEncoderPost = dynamic_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(encoder.get());
                auto* shadowTexturePost = dynamic_cast<RHI::Vulkan::VulkanRHITexture*>(shadowRenderer->getShadowMapTexture());
                if (vulkanEncoderPost && shadowTexturePost) {
                    vulkanEncoderPost->getCommandBuffer().pipelineBarrier(
                        vk::PipelineStageFlagBits::eLateFragmentTests,
                        vk::PipelineStageFlagBits::eFragmentShader,
                        {}, {}, {},
                        vk::ImageMemoryBarrier{
                            .srcAccessMask       = vk::AccessFlagBits::eDepthStencilAttachmentWrite,
                            .dstAccessMask       = vk::AccessFlagBits::eShaderRead,
                            .oldLayout           = vk::ImageLayout::eDepthStencilAttachmentOptimal,
                            .newLayout           = vk::ImageLayout::eShaderReadOnlyOptimal,
                            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                            .image               = shadowTexturePost->getVkImage(),
                            .subresourceRange    = vk::ImageSubresourceRange{
                                vk::ImageAspectFlagBits::eDepth, 0, 1, 0,
                                rendering::ShadowRenderer::NUM_CASCADES
                            }
                        }
                    );
                }
#endif
            }
#ifndef __EMSCRIPTEN__
            if (gpuProfiler) {
                auto* ve = dynamic_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(encoder.get());
                if (ve) gpuProfiler->endTimer(ve->getCommandBuffer(), frameIndex,
                    GpuProfiler::TimerId::ShadowPass);
            }
#endif
        }
    }

    // Get swapchain view
    auto* swapchainView = rhiBridge->getCurrentSwapchainView();
    if (!swapchainView) {
        return;
    }

    // [Phase 2 RenderGraph] Swapchain UNDEFINED→ColorAttachment transition is now
    // handled automatically by the render graph before the post-process pass.

    // Setup render pass
    rhi::RenderPassDesc renderPassDesc;
    renderPassDesc.width = rhiBridge->getSwapchain()->getWidth();
    renderPassDesc.height = rhiBridge->getSwapchain()->getHeight();
    renderPassDesc.label = "RHI Main Render Pass";

    // Color attachment: all platforms render geometry to HDR offscreen texture
    rhi::RenderPassColorAttachment colorAttachment;
    colorAttachment.view = hdrColorView ? hdrColorView.get() : swapchainView;
    colorAttachment.loadOp = rhi::LoadOp::Clear;
    colorAttachment.storeOp = rhi::StoreOp::Store;
    colorAttachment.clearValue = rhi::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
    renderPassDesc.colorAttachments.push_back(colorAttachment);

    // Depth attachment (if available)
    rhi::RenderPassDepthStencilAttachment depthAttachment;
    if (rhiDepthImageView) {
        depthAttachment.view = rhiDepthImageView.get();
        depthAttachment.depthLoadOp = rhi::LoadOp::Clear;
        depthAttachment.depthStoreOp = rhi::StoreOp::Store;
        depthAttachment.depthClearValue = 1.0f;
        renderPassDesc.depthStencilAttachment = &depthAttachment;
    }

    // Linux: use HDR render pass + HDR framebuffer for the geometry pass
#ifdef __linux__
    auto* rhiVulkanSwapchain = dynamic_cast<RHI::Vulkan::VulkanRHISwapchain*>(rhiBridge->getSwapchain());
    if (rhiVulkanSwapchain) {
        VkRenderPass vkPass         = static_cast<VkRenderPass>(rhiVulkanSwapchain->getHDRRenderPass());
        VkFramebuffer vkFramebuffer = static_cast<VkFramebuffer>(rhiVulkanSwapchain->getHDRFramebuffer());
        renderPassDesc.nativeRenderPass   = reinterpret_cast<void*>(vkPass);
        renderPassDesc.nativeFramebuffer  = reinterpret_cast<void*>(vkFramebuffer);
    }
#endif

    // Phase 4.1: GPU Profiling — begin main render pass timer
#ifndef __EMSCRIPTEN__
    if (gpuProfiler) {
        auto* ve = dynamic_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(encoder.get());
        if (ve) gpuProfiler->beginTimer(ve->getCommandBuffer(), frameIndex,
            GpuProfiler::TimerId::GBufferPass);
    }
#endif

    // Record commands
    auto renderPass = encoder->beginRenderPass(renderPassDesc);
    if (renderPass) {
        renderPass->setViewport(0.0f, 0.0f,
            static_cast<float>(renderPassDesc.width),
            static_cast<float>(renderPassDesc.height),
            0.0f, 1.0f);
        renderPass->setScissorRect(0, 0, renderPassDesc.width, renderPassDesc.height);

        // Phase 3.3: Render skybox first (background)
        if (skyboxRenderer) {
            // Calculate inverse view-projection matrix for ray direction
            glm::mat4 viewProj = projectionMatrix * viewMatrix;
            glm::mat4 invViewProj = glm::inverse(viewProj);

            // Calculate elapsed time for animation
            auto currentTime = std::chrono::high_resolution_clock::now();
            float time = std::chrono::duration<float>(currentTime - startTime).count();

            // Update skybox with current sun direction (for sun disk rendering)
            skyboxRenderer->setSunDirection(sunDirection);

            skyboxRenderer->render(renderPass.get(), frameIndex, invViewProj, time);
        }

        // Bind pipeline (if created)
        if (rhiPipeline) {
            renderPass->setPipeline(rhiPipeline.get());

            // Bind descriptor sets (bind groups)
            if (frameIndex < rhiBindGroups.size() && rhiBindGroups[frameIndex]) {
                renderPass->setBindGroup(0, rhiBindGroups[frameIndex].get());
            }

            // Phase 4.5: Bind vertex/index buffers and draw
            if (rhiVertexBuffer && rhiIndexBuffer && rhiIndexCount > 0) {
                renderPass->setVertexBuffer(0, rhiVertexBuffer.get(), 0);
                renderPass->setIndexBuffer(rhiIndexBuffer.get(), rhi::IndexFormat::Uint32, 0);
                renderPass->drawIndexed(rhiIndexCount, 1, 0, 0, 0);
            }
        }

        // Phase 3: In deferred path, geometry is in GBufferPass — skip here.
        // Keep skybox only in this pass (it writes to HDR as background).
        // Particles still go here (forward, after deferred lighting — handled below).

        renderPass->end();
    }

#ifdef __EMSCRIPTEN__
    using _Clock = std::chrono::high_resolution_clock;
    auto _tFrame = _Clock::now();
    auto _tPass  = _tFrame;

    // Real GPU timestamps via timestamp-query (no-op when unsupported).
    // The timer arms the next render/compute pass with begin+end timestamp
    // writes via the WebGPU encoder's setPendingTimestamps state setter.
    RHI::WebGPU::WebGPURHICommandEncoder* _wgpuEnc = nullptr;
    WGPUCommandEncoder _rawEnc = nullptr;
    if (m_webgpuTimer) {
        _wgpuEnc = dynamic_cast<RHI::WebGPU::WebGPURHICommandEncoder*>(encoder.get());
        if (_wgpuEnc) _rawEnc = _wgpuEnc->getWGPUEncoder();
        m_webgpuTimer->beginPhase(_wgpuEnc, WebGPUTimer::TimerId::GBuffer);
    }
#endif

    // G-Buffer pass — geometry to G-Buffers + depth (Vulkan + WebGPU)
    if (gBufferPass && gBufferPass->isInitialized()
        && pendingInstancedData && pendingInstancedData->instanceCount > 0) {
        auto* mesh         = pendingInstancedData->mesh;
        auto* objectBuffer = pendingInstancedData->objectBuffer;
        uint32_t W = rhiBridge->getSwapchain()->getWidth();
        uint32_t H = rhiBridge->getSwapchain()->getHeight();
        if (mesh && mesh->hasData() && objectBuffer) {
            rhi::RHIBindGroup* bg0 = (frameIndex < buildingBindGroups.size())
                                     ? buildingBindGroups[frameIndex].get() : nullptr;
            VkDescriptorSet bindlessSet = VK_NULL_HANDLE;
#ifndef __EMSCRIPTEN__
            if (bindlessTextureManager && bindlessTextureManager->isAvailable())
                bindlessSet = bindlessTextureManager->getVkDescriptorSet();
#endif
            rhi::RHIBindGroup* scBG  = showcaseAsset.isReady() ? showcaseAsset.ssboBindGroup.get()        : nullptr;
            rhi::RHIBuffer*    scVB  = showcaseAsset.isReady() ? showcaseAsset.mesh->getVertexBuffer()   : nullptr;
            rhi::RHIBuffer*    scIB  = showcaseAsset.isReady() ? showcaseAsset.mesh->getIndexBuffer()    : nullptr;
            uint32_t           scIdx = showcaseAsset.isReady() ? showcaseAsset.indexCount                : 0u;

            rhi::RHIBindGroup* matDefaultBG  = nullptr;
            rhi::RHIBindGroup* matShowcaseBG = nullptr;
#ifdef __EMSCRIPTEN__
            matDefaultBG  = defaultMaterialBindGroup.get();
            matShowcaseBG = showcaseAsset.materialBindGroup.get();
#endif

            gBufferPass->execute(encoder.get(),
                                 bg0,
                                 ssboBindGroups[frameIndex].get(),
                                 mesh->getVertexBuffer(),
                                 mesh->getIndexBuffer(),
                                 indirectDrawBuffers[frameIndex].get(),
                                 W, H,
                                 bindlessSet,
                                 scBG, scVB, scIB, scIdx,
                                 matDefaultBG, matShowcaseBG);
        }
    }
    if (pendingInstancedData) pendingInstancedData.reset();

#ifdef __EMSCRIPTEN__
    if (m_webgpuTimer) {
        m_webgpuTimer->beginPhase(_wgpuEnc, WebGPUTimer::TimerId::Deferred);
    }
    m_passTimeGBuffer = std::chrono::duration<float, std::milli>(_Clock::now() - _tPass).count();
    _tPass = _Clock::now();

    // Deferred Lighting pass — WebGPU sequential path
    // (Vulkan executes this via RenderGraph in the section below)
    if (deferredLightingPass && deferredLightingPass->isInitialized()
        && gBufferPass && gBufferPass->isInitialized()) {
        uint32_t dlW = rhiBridge->getSwapchain()->getWidth();
        uint32_t dlH = rhiBridge->getSwapchain()->getHeight();
        rhi::RenderPassDesc dlDesc;
        dlDesc.width  = dlW;
        dlDesc.height = dlH;
        dlDesc.label  = "DeferredLighting";
        rhi::RenderPassColorAttachment dlColor;
        dlColor.view     = hdrColorView.get();
        dlColor.loadOp   = rhi::LoadOp::Load;
        dlColor.storeOp  = rhi::StoreOp::Store;
        dlDesc.colorAttachments.push_back(dlColor);
        auto dlPass = encoder->beginRenderPass(dlDesc);
        if (dlPass) {
            dlPass->setViewport(0.0f, 0.0f, float(dlW), float(dlH), 0.0f, 1.0f);
            dlPass->setScissorRect(0, 0, dlW, dlH);
            dlPass->setPipeline(deferredLightingPass->getPipeline());
            dlPass->setBindGroup(0, deferredLightingPass->getBindGroup(frameIndex));
            dlPass->draw(3);  // Fullscreen triangle
            dlPass->end();
        }
    }
    if (m_webgpuTimer) {
        m_webgpuTimer->beginPhase(_wgpuEnc, WebGPUTimer::TimerId::SSAO);
    }
    m_passTimeDeferred = std::chrono::duration<float, std::milli>(_Clock::now() - _tPass).count();
    _tPass = _Clock::now();
#endif

    // Phase 4.1: GPU Profiling — end main render pass timer
#ifndef __EMSCRIPTEN__
    if (gpuProfiler) {
        auto* ve = dynamic_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(encoder.get());
        if (ve) gpuProfiler->endTimer(ve->getCommandBuffer(), frameIndex,
            GpuProfiler::TimerId::GBufferPass);
    }
#endif

#ifdef __EMSCRIPTEN__
    // SSAO pass (WebGPU): depth → half-res R8Unorm AO, then bilateral blur
    if (wgslSSAOPipeline && wgslSSAOBlurPipeline && ssaoTextureView && ssaoBlurView) {
        auto ssaoSize = ssaoTexture ? ssaoTexture->getSize() : rhi::Extent3D{1, 1, 1};
        uint32_t sW = ssaoSize.width;
        uint32_t sH = ssaoSize.height;

        // Update SSAOParams UBO each frame (projection matrix + camera params)
        {
            struct alignas(16) SSAOParams {
                glm::mat4 projection;
                float radius, bias, near, far, invW, invH, pad0, pad1;
            };
            SSAOParams pc{};
            pc.projection = projectionMatrix;
            pc.radius = 0.5f; pc.bias = 0.025f;
            pc.near   = 0.1f; pc.far  = 1000.0f;
            pc.invW   = 1.0f / static_cast<float>(sW);
            pc.invH   = 1.0f / static_cast<float>(sH);
            wgslSSAOParamsUBO->write(&pc, sizeof(pc));
        }

        // Update SSAOBlurParams UBO
        {
            struct SSAOBlurParams { float invW, invH, near, far, depthThreshold, pad0, pad1, pad2; };
            SSAOBlurParams bp{};
            bp.invW = 1.0f / static_cast<float>(sW);
            bp.invH = 1.0f / static_cast<float>(sH);
            bp.near = 0.1f; bp.far = 1000.0f;
            bp.depthThreshold = 0.05f;
            wgslSSAOBlurParamsUBO->write(&bp, sizeof(bp));
        }

        // 1. SSAO pass: depth → ssaoTexture (raw AO)
        {
            rhi::RenderPassDesc pd;
            pd.width = sW; pd.height = sH; pd.label = "SSAO";
            rhi::RenderPassColorAttachment ca;
            ca.view = ssaoTextureView.get();
            ca.loadOp = rhi::LoadOp::Clear; ca.storeOp = rhi::StoreOp::Store;
            ca.clearValue = rhi::ClearColorValue(1.0f, 0.0f, 0.0f, 1.0f);  // 1.0 = no occlusion
            pd.colorAttachments.push_back(ca);
            auto pass = encoder->beginRenderPass(pd);
            if (pass) {
                pass->setViewport(0, 0, float(sW), float(sH), 0, 1);
                pass->setScissorRect(0, 0, sW, sH);
                pass->setPipeline(wgslSSAOPipeline.get());
                pass->setBindGroup(0, wgslSSAOBG.get());
                pass->draw(3);
                pass->end();
            }
        }

        // 2. SSAO blur pass: ssaoTexture + depth → ssaoBlurTexture (bilateral filtered AO)
        {
            rhi::RenderPassDesc pd;
            pd.width = sW; pd.height = sH; pd.label = "SSAOBlur";
            rhi::RenderPassColorAttachment ca;
            ca.view = ssaoBlurView.get();
            ca.loadOp = rhi::LoadOp::Clear; ca.storeOp = rhi::StoreOp::Store;
            ca.clearValue = rhi::ClearColorValue(1.0f, 0.0f, 0.0f, 1.0f);
            pd.colorAttachments.push_back(ca);
            auto pass = encoder->beginRenderPass(pd);
            if (pass) {
                pass->setViewport(0, 0, float(sW), float(sH), 0, 1);
                pass->setScissorRect(0, 0, sW, sH);
                pass->setPipeline(wgslSSAOBlurPipeline.get());
                pass->setBindGroup(0, wgslSSAOBlurBG.get());
                pass->draw(3);
                pass->end();
            }
        }
    }
    if (m_webgpuTimer) {
        m_webgpuTimer->beginPhase(_wgpuEnc, WebGPUTimer::TimerId::Bloom);
    }
    m_passTimeSSAO = std::chrono::duration<float, std::milli>(_Clock::now() - _tPass).count();
    _tPass = _Clock::now();

    // Bloom passes (WebGPU): prefilter HDR → half-res, then 2× separable Gaussian blur
    if (wgslBloomPrefilterPipeline && bloomTextureView && bloomPingView) {
        auto bloomSize = bloomTexture ? bloomTexture->getSize() : rhi::Extent3D{1, 1, 1};
        uint32_t bW = bloomSize.width;
        uint32_t bH = bloomSize.height;

        auto runBloomPass = [&](rhi::RHIRenderPipeline* pipeline,
                                rhi::RHITextureView*    target,
                                rhi::RHIBindGroup*      bg,
                                const char*             label) {
            rhi::RenderPassDesc pd;
            pd.width = bW; pd.height = bH; pd.label = label;
            rhi::RenderPassColorAttachment ca;
            ca.view = target;
            ca.loadOp = rhi::LoadOp::Clear;
            ca.storeOp = rhi::StoreOp::Store;
            ca.clearValue = rhi::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
            pd.colorAttachments.push_back(ca);
            auto pass = encoder->beginRenderPass(pd);
            if (pass) {
                pass->setViewport(0, 0, float(bW), float(bH), 0, 1);
                pass->setScissorRect(0, 0, bW, bH);
                pass->setPipeline(pipeline);
                pass->setBindGroup(0, bg);
                pass->draw(3);
                pass->end();
            }
        };

        // 1. Prefilter: HDR → bloomTexture
        runBloomPass(wgslBloomPrefilterPipeline.get(), bloomTextureView.get(),
                     wgslBloomPrefilterBG.get(), "BloomPrefilter");

        // 2. Two iterations of separable blur: bloom → ping (H), ping → bloom (V)
        for (int i = 0; i < 2; ++i) {
            runBloomPass(wgslBloomBlurHPipeline.get(), bloomPingView.get(),
                         wgslBloomBlurBGs[0].get(), "BloomBlurH");
            runBloomPass(wgslBloomBlurVPipeline.get(), bloomTextureView.get(),
                         wgslBloomBlurBGs[1].get(), "BloomBlurV");
        }
    }
    if (m_webgpuTimer) {
        m_webgpuTimer->beginPhase(_wgpuEnc, WebGPUTimer::TimerId::PostProcess);
    }
    m_passTimeBloom = std::chrono::duration<float, std::milli>(_Clock::now() - _tPass).count();
    _tPass = _Clock::now();

    // PostProcess pass (WebGPU): HDR + Bloom + SSAO → ACES + FXAA → swapchain
    if (wgslPostprocessPipeline && wgslPostprocessBG && hdrColorView && swapchainView) {
        // Update params UBO each frame
        struct alignas(16) PostProcessParams {
            float    bloomStrength;
            float    exposure;
            float    aoStrength;
            int32_t  debugView;
            uint32_t fxaaOn;
            uint32_t tonemapOn;
            float    texelW;
            float    texelH;
            float    abSplitX;       // 0 = off, (0,1) = uv.x split
            float    _pad0;
            float    _pad1;
            float    _pad2;
        };
        auto* sc = rhiBridge->getSwapchain();
        PostProcessParams pp{};
        pp.bloomStrength = bloomStrength;
        pp.exposure      = exposure;
        pp.aoStrength    = aoStrength;
        pp.debugView     = debugView;
        pp.fxaaOn        = fxaaEnabled    ? 1u : 0u;
        pp.tonemapOn     = tonemapEnabled ? 1u : 0u;
        pp.texelW        = sc ? 1.0f / static_cast<float>(sc->getWidth())  : 0.0f;
        pp.texelH        = sc ? 1.0f / static_cast<float>(sc->getHeight()) : 0.0f;
        pp.abSplitX      = abSplitX;
        wgslPostprocessParamsUBO->write(&pp, sizeof(pp));

        uint32_t W = sc ? sc->getWidth()  : 1;
        uint32_t H = sc ? sc->getHeight() : 1;

        rhi::RenderPassDesc pd;
        pd.width = W; pd.height = H; pd.label = "PostProcess";
        rhi::RenderPassColorAttachment ca;
        ca.view      = swapchainView;
        ca.loadOp    = rhi::LoadOp::Clear;
        ca.storeOp   = rhi::StoreOp::Store;
        ca.clearValue = rhi::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
        pd.colorAttachments.push_back(ca);

        auto pass = encoder->beginRenderPass(pd);
        if (pass) {
            pass->setViewport(0.0f, 0.0f, float(W), float(H), 0.0f, 1.0f);
            pass->setScissorRect(0, 0, W, H);
            pass->setPipeline(wgslPostprocessPipeline.get());
            pass->setBindGroup(0, wgslPostprocessBG.get());
            pass->draw(3);
            pass->end();
        }
    }
    m_passTimePostProcess = std::chrono::duration<float, std::milli>(_Clock::now() - _tPass).count();
#endif  // __EMSCRIPTEN__

#ifndef __EMSCRIPTEN__
    // =========================================================================
    // Phase 2: Render Graph — SSAO, Bloom, PostProcess, Present transitions
    // Barriers between passes are inferred automatically from read/write deps.
    // =========================================================================
    {
        using RA = rendergraph::RGAccess;
        using PT = rendergraph::RGPassType;
        constexpr auto INVALID = rendergraph::RG_INVALID_RESOURCE;

        uint32_t W = rhiBridge->getSwapchain()->getWidth();
        uint32_t H = rhiBridge->getSwapchain()->getHeight();
        uint32_t sW = std::max(1u, W / 2);
        uint32_t sH = std::max(1u, H / 2);
        uint32_t bW = std::max(1u, W / 2);
        uint32_t bH = std::max(1u, H / 2);

        m_renderGraph.reset();

        // Import states reflect what each texture is in after the manual passes.
        // Phase 3: HDR → ColorAttachment (skybox pass), Depth → DepthStencilAttachment (G-Buffer)
        // G-Buffers → ColorAttachment (G-Buffer pass), others → Undefined.
        rendergraph::RGTexState depthInitial = rendergraph::RenderGraph::inferTexState(RA::DepthWrite);
        rendergraph::RGTexState hdrInitial   = rendergraph::RenderGraph::inferTexState(RA::ColorWrite);
        rendergraph::RGTexState gbufInitial  = rendergraph::RenderGraph::inferTexState(RA::ColorWrite);
        rendergraph::RGTexState undefinedSt{};

        auto rgHDR   = m_renderGraph.importTexture("HDR",   hdrColorTexture.get(), hdrInitial);
        auto rgDepth = m_renderGraph.importTexture("Depth", rhiDepthImage.get(),   depthInitial);

        // Phase 3: G-Buffer resources (optional, Vulkan deferred path only)
        rendergraph::RGResourceHandle rgGBuffer0 = INVALID;
        rendergraph::RGResourceHandle rgGBuffer1 = INVALID;
        rendergraph::RGResourceHandle rgGBuffer2 = INVALID;
        if (gBufferPass && gBufferPass->isInitialized()) {
            rgGBuffer0 = m_renderGraph.importTexture("GBuffer0", gBufferPass->getGBuffer0(), gbufInitial);
            rgGBuffer1 = m_renderGraph.importTexture("GBuffer1", gBufferPass->getGBuffer1(), gbufInitial);
            rgGBuffer2 = m_renderGraph.importTexture("GBuffer2", gBufferPass->getGBuffer2(), gbufInitial);
        }

        // SSAO resources (optional)
        rendergraph::RGResourceHandle rgSSAO    = INVALID;
        rendergraph::RGResourceHandle rgSSAOBlur = INVALID;
        if (ssaoPipeline && ssaoTexture && ssaoBlurTexture) {
            rgSSAO     = m_renderGraph.importTexture("SSAO",    ssaoTexture.get(),    undefinedSt);
            rgSSAOBlur = m_renderGraph.importTexture("SSAOBlur", ssaoBlurTexture.get(), undefinedSt);
        }

        // Bloom resources (optional)
        rendergraph::RGResourceHandle rgBloom    = INVALID;
        rendergraph::RGResourceHandle rgBloomPing = INVALID;
        if (bloomThresholdPipeline && bloomTexture && bloomPingTexture) {
            rgBloom    = m_renderGraph.importTexture("Bloom",    bloomTexture.get(),    undefinedSt);
            rgBloomPing = m_renderGraph.importTexture("BloomPing", bloomPingTexture.get(), undefinedSt);
        }

        // Swapchain image (macOS/Windows only — Linux render pass handles transitions)
        rendergraph::RGResourceHandle rgSwapchain = INVALID;
#if !defined(__linux__)
        if (auto* vulkanSC = dynamic_cast<RHI::Vulkan::VulkanRHISwapchain*>(swapchain)) {
            vk::Image scImage = vulkanSC->getCurrentVkImage();
            if (scImage) {
                rgSwapchain = m_renderGraph.importSwapchainImage(
                    "Swapchain", scImage, false, 1, 1, undefinedSt);
            }
        }
#endif

        // ---- SSAO passes ----
        if (ssaoPipeline && ssaoBlurPipeline && ssaoBindGroup && ssaoBlurBindGroup
            && rgSSAO != INVALID)
        {
            {   // SSAO pass — begins SSAOPass timer
                uint32_t ssaoFI = frameIndex;
                auto pass = m_renderGraph.addPass("SSAO", PT::Compute,
                    [this, sW, sH, ssaoFI](rhi::RHICommandEncoder* enc) {
                        auto* ve = dynamic_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(enc);
                        if (gpuProfiler && ve) gpuProfiler->beginTimer(ve->getCommandBuffer(), ssaoFI,
                            GpuProfiler::TimerId::SSAOPass, vk::PipelineStageFlagBits::eComputeShader);
                        auto ce = enc->beginComputePass("SSAO");
                        ce->setPipeline(ssaoPipeline.get());
                        ce->setBindGroup(0, ssaoBindGroup.get());
                        struct SSAOPC {
                            glm::mat4 projection;
                            float radius; float bias; float near; float far;
                            float invW; float invH; float pad[2];
                        };
                        SSAOPC pc{};
                        pc.projection = projectionMatrix;
                        pc.radius = 0.5f; pc.bias = 0.025f;
                        pc.near = 0.1f;   pc.far  = 1000.0f;
                        pc.invW = 1.0f / static_cast<float>(sW);
                        pc.invH = 1.0f / static_cast<float>(sH);
                        ce->setPushConstants(ssaoPipelineLayout.get(),
                            rhi::ShaderStage::Compute, 0, sizeof(pc), &pc);
                        ce->dispatch((sW + 7) / 8, (sH + 7) / 8, 1);
                        ce->end();
                    });
                m_renderGraph.addReadDep(pass, rgDepth, RA::SampleCompute);
                m_renderGraph.addWriteDep(pass, rgSSAO,  RA::StorageWrite);
            }
            {   // SSAO blur pass — ends SSAOPass timer
                uint32_t ssaoFI = frameIndex;
                auto pass = m_renderGraph.addPass("SSAOBlur", PT::Compute,
                    [this, sW, sH, ssaoFI](rhi::RHICommandEncoder* enc) {
                        auto ce = enc->beginComputePass("SSAO Blur");
                        ce->setPipeline(ssaoBlurPipeline.get());
                        ce->setBindGroup(0, ssaoBlurBindGroup.get());
                        struct SSAOBlurPC { float invW; float invH; float near; float far; float depthThreshold; float pad; };
                        SSAOBlurPC pc{ 1.0f / sW, 1.0f / sH, 0.1f, 1000.0f, 0.05f, 0.0f };
                        ce->setPushConstants(ssaoBlurPipelineLayout.get(),
                            rhi::ShaderStage::Compute, 0, sizeof(pc), &pc);
                        ce->dispatch((sW + 7) / 8, (sH + 7) / 8, 1);
                        ce->end();
                        auto* ve = dynamic_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(enc);
                        if (gpuProfiler && ve) gpuProfiler->endTimer(ve->getCommandBuffer(), ssaoFI,
                            GpuProfiler::TimerId::SSAOPass, vk::PipelineStageFlagBits::eComputeShader);
                    });
                m_renderGraph.addReadDep(pass,  rgSSAO,    RA::SampleCompute);
                m_renderGraph.addWriteDep(pass, rgSSAOBlur, RA::StorageWrite);
            }
        }

        // ---- Phase 3: Deferred Lighting pass (G-Buffers → HDR, after SSAO) ----
        if (deferredLightingPass && deferredLightingPass->isInitialized()
            && rgGBuffer0 != INVALID) {
            rhi::RenderPassDesc dlDesc;
            dlDesc.width  = W;
            dlDesc.height = H;
            dlDesc.label  = "DeferredLighting";

            rhi::RenderPassColorAttachment dlColor;
            dlColor.view       = hdrColorView.get();
            dlColor.loadOp     = rhi::LoadOp::Load;   // preserve skybox already in HDR
            dlColor.storeOp    = rhi::StoreOp::Store;
            dlColor.clearValue = rhi::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
            dlDesc.colorAttachments.push_back(dlColor);

#ifdef __linux__
            if (auto* rhiVulkanSC = dynamic_cast<RHI::Vulkan::VulkanRHISwapchain*>(swapchain)) {
                // Use the Load variant so the skybox rendered in the first HDR pass is preserved
                dlDesc.nativeRenderPass  = reinterpret_cast<void*>(
                    static_cast<VkRenderPass>(rhiVulkanSC->getHDRLoadRenderPass()));
                dlDesc.nativeFramebuffer = reinterpret_cast<void*>(
                    static_cast<VkFramebuffer>(rhiVulkanSC->getHDRFramebuffer()));
            }
#endif

            uint32_t dlFI = frameIndex;
            auto pass = m_renderGraph.addPass("DeferredLighting", PT::Render,
                [this, dlDesc, W, H, dlFI](rhi::RHICommandEncoder* enc) {
                    auto* ve = dynamic_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(enc);
                    if (gpuProfiler && ve) gpuProfiler->beginTimer(ve->getCommandBuffer(), dlFI,
                        GpuProfiler::TimerId::DeferredLighting);
                    auto dlPass = enc->beginRenderPass(dlDesc);
                    if (dlPass) {
                        dlPass->setViewport(0.0f, 0.0f, float(W), float(H), 0.0f, 1.0f);
                        dlPass->setScissorRect(0, 0, W, H);
                        dlPass->setPipeline(deferredLightingPass->getPipeline());
                        dlPass->setBindGroup(0, deferredLightingPass->getBindGroup(dlFI));
                        dlPass->draw(3);  // Fullscreen triangle
                        dlPass->end();
                    }
                    if (gpuProfiler && ve) gpuProfiler->endTimer(ve->getCommandBuffer(), dlFI,
                        GpuProfiler::TimerId::DeferredLighting);
                });
            // Reads
            m_renderGraph.addReadDep(pass, rgGBuffer0, RA::SampleFragment);
            m_renderGraph.addReadDep(pass, rgGBuffer1, RA::SampleFragment);
            m_renderGraph.addReadDep(pass, rgGBuffer2, RA::SampleFragment);
            m_renderGraph.addReadDep(pass, rgDepth,    RA::SampleFragment);
            if (rgSSAOBlur != INVALID) m_renderGraph.addReadDep(pass, rgSSAOBlur, RA::SampleFragment);
            // Write HDR
            m_renderGraph.addWriteDep(pass, rgHDR, RA::ColorWrite);
        }

        // ---- Bloom passes ----
        if (bloomThresholdPipeline && bloomBlurPipeline && rgBloom != INVALID) {
            {   // Bloom threshold pass (HDR → half-res bright-pass) — begins BloomPass timer
                uint32_t bloomFI = frameIndex;
                auto pass = m_renderGraph.addPass("BloomThreshold", PT::Compute,
                    [this, bW, bH, bloomFI](rhi::RHICommandEncoder* enc) {
                        auto* ve = dynamic_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(enc);
                        if (gpuProfiler && ve) gpuProfiler->beginTimer(ve->getCommandBuffer(), bloomFI,
                            GpuProfiler::TimerId::BloomPass, vk::PipelineStageFlagBits::eComputeShader);
                        auto ce = enc->beginComputePass("Bloom Threshold");
                        ce->setPipeline(bloomThresholdPipeline.get());
                        ce->setBindGroup(0, bloomThresholdBindGroup.get());
                        struct BloomThresholdPC { float invW; float invH; float threshold; float knee; };
                        BloomThresholdPC pc{ 1.0f / bW, 1.0f / bH, 1.0f, 0.5f };
                        ce->setPushConstants(bloomThresholdPipelineLayout.get(),
                            rhi::ShaderStage::Compute, 0, sizeof(pc), &pc);
                        ce->dispatch((bW + 7) / 8, (bH + 7) / 8, 1);
                        ce->end();
                    });
                m_renderGraph.addReadDep(pass, rgHDR,  RA::SampleCompute);
                m_renderGraph.addWriteDep(pass, rgBloom, RA::StorageWrite);
            }

            // 4× Dual Kawase blur passes
            // iter=0: reads rgBloom,    writes rgBloomPing
            // iter=1: reads rgBloomPing, writes rgBloom
            // iter=2: reads rgBloom,    writes rgBloomPing
            // iter=3: reads rgBloomPing, writes rgBloom  ← final result in rgBloom
            for (int iter = 0; iter < 4; ++iter) {
                int pingPong = iter % 2;
                rendergraph::RGResourceHandle readTex  = (pingPong == 0) ? rgBloom     : rgBloomPing;
                rendergraph::RGResourceHandle writeTex = (pingPong == 0) ? rgBloomPing : rgBloom;

                uint32_t bloomFI2 = frameIndex;
                auto pass = m_renderGraph.addPass("BloomBlur", PT::Compute,
                    [this, bW, bH, iter, pingPong, bloomFI2](rhi::RHICommandEncoder* enc) {
                        auto ce = enc->beginComputePass("Bloom Blur");
                        ce->setPipeline(bloomBlurPipeline.get());
                        ce->setBindGroup(0, bloomBlurBindGroups[pingPong].get());
                        struct BloomBlurPC { float invW; float invH; int blurIter; float pad; };
                        BloomBlurPC pc{ 1.0f / bW, 1.0f / bH, iter, 0.0f };
                        ce->setPushConstants(bloomBlurPipelineLayout.get(),
                            rhi::ShaderStage::Compute, 0, sizeof(pc), &pc);
                        ce->dispatch((bW + 7) / 8, (bH + 7) / 8, 1);
                        ce->end();
                        // End BloomPass timer after the last blur iteration
                        if (iter == 3) {
                            auto* ve = dynamic_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(enc);
                            if (gpuProfiler && ve) gpuProfiler->endTimer(ve->getCommandBuffer(), bloomFI2,
                                GpuProfiler::TimerId::BloomPass, vk::PipelineStageFlagBits::eComputeShader);
                        }
                    });
                m_renderGraph.addReadDep(pass, readTex,  RA::SampleCompute);
                m_renderGraph.addWriteDep(pass, writeTex, RA::StorageWrite);
            }
        }

        // ---- Post-process pass (Tonemap + FXAA → swapchain) ----
        if (postprocessPipeline && postprocessBindGroup) {
            // Build ppDesc outside lambda so it can be captured by value
            rhi::RenderPassDesc ppDesc;
            ppDesc.width  = W;
            ppDesc.height = H;
            ppDesc.label  = "PostProcess Pass";
            rhi::RenderPassColorAttachment ppColor;
            ppColor.view       = swapchainView;
            ppColor.loadOp     = rhi::LoadOp::DontCare;
            ppColor.storeOp    = rhi::StoreOp::Store;
            ppColor.clearValue = rhi::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
            ppDesc.colorAttachments.push_back(ppColor);
#ifdef __linux__
            if (auto* rhiVulkanSC = dynamic_cast<RHI::Vulkan::VulkanRHISwapchain*>(swapchain)) {
                uint32_t idx = rhiBridge->getCurrentImageIndex();
                ppDesc.nativeRenderPass  = reinterpret_cast<void*>(
                    static_cast<VkRenderPass>(rhiVulkanSC->getPostProcessRenderPass()));
                ppDesc.nativeFramebuffer = reinterpret_cast<void*>(
                    static_cast<VkFramebuffer>(rhiVulkanSC->getPostProcessFramebuffer(idx)));
            }
#endif
            uint32_t imgIdx = rhiBridge->getCurrentImageIndex();

            auto pass = m_renderGraph.addPass("PostProcess", PT::Render,
                [this, ppDesc, W, H, frameIndex, imgIdx](rhi::RHICommandEncoder* enc) {
                    auto* ve = dynamic_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(enc);
                    if (gpuProfiler && ve) gpuProfiler->beginTimer(ve->getCommandBuffer(), frameIndex,
                        GpuProfiler::TimerId::PostProcess);
                    auto ppPass = enc->beginRenderPass(ppDesc);
                    if (ppPass) {
                        ppPass->setViewport(0.0f, 0.0f, static_cast<float>(W),
                            static_cast<float>(H), 0.0f, 1.0f);
                        ppPass->setScissorRect(0, 0, W, H);
                        ppPass->setPipeline(postprocessPipeline.get());
                        ppPass->setBindGroup(0, postprocessBindGroup.get());
                        struct PostProcessPC {
                            float texelW, texelH;
                            float bloomStr, exposure, aoStr;
                            float tonemapOn, debugViewF, fxaaOn;
                        };
                        float effectiveAO = ssaoPipeline ? aoStrength : 0.0f;
                        PostProcessPC pc{
                            1.0f / W, 1.0f / H,
                            bloomStrength, exposure, effectiveAO,
                            tonemapEnabled ? 1.0f : 0.0f,
                            static_cast<float>(debugView),
                            fxaaEnabled ? 1.0f : 0.0f
                        };
                        ppPass->setPushConstants(postprocessPipelineLayout.get(),
                            rhi::ShaderStage::Fragment, 0, sizeof(pc), &pc);
                        ppPass->draw(3);
                        if (imguiManager)
                            imguiManager->render(enc, imgIdx);
                        ppPass->end();
                    }
                    if (gpuProfiler && ve) gpuProfiler->endTimer(ve->getCommandBuffer(), frameIndex,
                        GpuProfiler::TimerId::PostProcess);
                });
            m_renderGraph.addReadDep(pass, rgHDR, RA::SampleFragment);
            if (rgBloom    != INVALID) m_renderGraph.addReadDep(pass, rgBloom,    RA::SampleFragment);
            if (rgSSAOBlur != INVALID) m_renderGraph.addReadDep(pass, rgSSAOBlur, RA::SampleFragment);
            if (rgSwapchain != INVALID) m_renderGraph.addWriteDep(pass, rgSwapchain, RA::ColorWrite);
        }

        // ---- Present transition (macOS/Windows — Linux render pass handles it) ----
#if !defined(__linux__)
        if (rgSwapchain != INVALID) {
            auto pass = m_renderGraph.addPass("Present", PT::Render,
                [](rhi::RHICommandEncoder*) { /* no-op: only triggers PresentSrc barrier */ });
            m_renderGraph.addReadDep(pass, rgSwapchain, RA::PresentSrc);
        }
#endif

        m_renderGraph.compile();
        m_renderGraph.execute(encoder.get());
    }
    // =========================================================================

#endif  // !__EMSCRIPTEN__ — render graph block

#ifdef __EMSCRIPTEN__
    // Resolve all phase timestamps before encoder finish.
    if (m_webgpuTimer) {
        m_webgpuTimer->endFrame(_rawEnc);
    }
#endif

    // Finish command buffer
    auto commandBuffer = encoder->finish();
#ifdef __EMSCRIPTEN__
    m_passTimeTotal = std::chrono::duration<float, std::milli>(_Clock::now() - _tFrame).count();
#endif

    // Step 4: Submit command buffer with synchronization
    if (commandBuffer) {
        if (useAsyncCompute && computeTimelineValue > 0) {
            // Async compute path: use SubmitInfo with timeline wait
            rhi::SubmitInfo graphicsSubmit;
            graphicsSubmit.commandBuffers.push_back(commandBuffer.get());
            graphicsSubmit.waitSemaphores.push_back(rhiBridge->getImageAvailableSemaphore());
            graphicsSubmit.signalSemaphores.push_back(rhiBridge->getRenderFinishedSemaphore());
            graphicsSubmit.signalFence = rhiBridge->getInFlightFence();
            graphicsSubmit.timelineWaits.push_back(
                rhi::TimelineWait{computeTimelineSemaphore.get(), computeTimelineValue});

            auto* graphicsQueue = rhiBridge->getDevice()->getQueue(rhi::QueueType::Graphics);
            graphicsQueue->submit(graphicsSubmit);
        } else {
            // Inline compute path: simple submit
            rhiBridge->submitCommandBuffer(
                commandBuffer.get(),
                rhiBridge->getImageAvailableSemaphore(),
                rhiBridge->getRenderFinishedSemaphore(),
                rhiBridge->getInFlightFence()
            );
        }
    }

    // Step 5: Present frame
    rhiBridge->endFrame();
}
