# 변경 이력 — 2026-04-09

> 작업 범위: Phase 3 완성 — Deferred Rendering (G-Buffer MRT) + 4-Cascade Shadow Maps (CSM)

---

## 1. 개요

포워드 렌더링 파이프라인을 **Deferred Rendering**으로 전환하고,
단일 2048×2048 그림자 맵을 **4-Cascade Depth Array Texture**로 대체했다.

**커밋:** `5de4069 feat(rendering): implement deferred rendering with 4-cascade CSM (Phase 3)`

**변경 규모:** 16개 파일, +1774 / -561 줄

---

## 2. Deferred Rendering — G-Buffer 파이프라인

### 2.1 G-Buffer 레이아웃

| 어태치먼트 | 포맷 | 내용 |
|---|---|---|
| GBuffer0 | `RGBA16Float` | 월드 공간 Normal (xyz) + Roughness |
| GBuffer1 | `RGBA8Unorm` | Albedo (rgb, linear) + Metallic |
| GBuffer2 | `RGBA8Unorm` | AO (r) + padding |
| Depth | `Depth32Float` | 기존 뎁스 버퍼 재활용 (재구성용) |

### 2.2 신규 셰이더

#### `shaders/gbuffer.vert.glsl`
- 지오메트리 패스: MVP 변환 + 월드 공간 법선 변환 (`transpose(inverse(mat3(worldMatrix)))`)
- SSBO `ObjectBuffer`에서 인스턴스별 재질 읽기 (albedo, metallic, roughness, AO)
- G-Buffer 셰이더는 기존 `building.vert.glsl`의 SSBO 바인딩 레이아웃 재사용

#### `shaders/gbuffer.frag.glsl`
- MRT 3개 출력: `layout(location = 0/1/2) out vec4`
- sRGB albedo → linear 변환 (`pow(albedo, 2.2)`)
- 조명 계산 없음 — 재질 데이터만 팩킹

#### `shaders/deferred_lighting.vert.glsl`
- 풀스크린 삼각형 (`gl_VertexIndex`로 UV 생성)
- 별도 버텍스 버퍼 불필요

#### `shaders/deferred_lighting.frag.glsl`
- G-Buffer 샘플링 후 Cook-Torrance BRDF 적용
- `depth == 1.0` 스카이박스 픽셀 `discard`
- **뎁스 재구성 → 월드 좌표:**
  ```glsl
  vec4 ndcPos  = vec4(uv * 2.0 - 1.0, depth, 1.0);
  vec4 viewPos = ubo.invProj * ndcPos;
  viewPos     /= viewPos.w;
  vec3 worldPos = vec3(ubo.invView * viewPos);
  ```
- IBL (Irradiance Map, Prefiltered Env, BRDF LUT) 적용
- CSM 4-캐스케이드 PCF 3×3 그림자 적용

**바인딩 (set 0, 12개):**
| 바인딩 | 타입 | 설명 |
|---|---|---|
| 0 | `UniformBuffer` | UBO (invView, invProj, lightSpaceMatrices[4], cascadeSplits 포함) |
| 1–3 | `SampledTexture` | GBuffer0/1/2 |
| 4 | `SampledTexture` | 뎁스 텍스처 |
| 5 | `Sampler` | G-Buffer 샘플러 |
| 6 | `SampledTexture` | CSM Depth Array (`texture2DArray`) |
| 7 | `Sampler` | 그림자 샘플러 |
| 8–10 | `SampledTexture` | IBL (irradiance, prefiltered, BRDF LUT) |
| 11 | `Sampler` | IBL 샘플러 |

### 2.3 신규 클래스: `GBufferPass`

**파일:** `src/rendering/GBufferPass.hpp/.cpp`

```cpp
class GBufferPass {
    bool initialize(uint32_t width, uint32_t height,
                    rhi::RHIBindGroupLayout* buildingBGLayout,
                    rhi::RHIBindGroupLayout* ssboLayout,
                    rhi::RHITextureView* depthView);
    void resize(uint32_t width, uint32_t height, rhi::RHITextureView* newDepthView);
    void execute(rhi::RHICommandEncoder* encoder, ...);

    rhi::RHITextureView* getGBuffer0View() const;
    rhi::RHITextureView* getGBuffer1View() const;
    rhi::RHITextureView* getGBuffer2View() const;
};
```

- 기존 building 파이프라인의 bind group layout (set 0, set 1) 재사용
- Linux: 별도 `VkRenderPass` + `VkFramebuffer` 생성 (`#ifdef __linux__`)
- MRT는 `RenderPassDesc.colorAttachments` 벡터로 3개 어태치먼트 설정

### 2.4 신규 클래스: `DeferredLightingPass`

**파일:** `src/rendering/DeferredLightingPass.hpp/.cpp`

```cpp
class DeferredLightingPass {
    bool initialize(
        const std::array<rhi::RHIBuffer*, MAX_FRAMES_IN_FLIGHT>& uniformBuffers,
        size_t uboSize,
        rhi::RHITextureView* gBuffer0View, gBuffer1View, gBuffer2View,
        rhi::RHITextureView* depthView, shadowCsmView, irradianceView, ...);

    rhi::RHIBindGroup* getBindGroup(uint32_t frameIndex) const;
};
```

- 풀스크린 패스: 버텍스 버퍼 없이 `drawIndexed(3, 1, ...)` 호출
- per-frame bind group 2개 (`MAX_FRAMES_IN_FLIGHT = 2`)

---

## 3. 4-Cascade Shadow Maps (CSM)

### 3.1 구조 변경

| 항목 | Phase 2 이전 | Phase 3 |
|---|---|---|
| 텍스처 | 2D 단일 (`2048×2048`) | 2D Array (`2048×2048×4`) |
| 캐스케이드 수 | 1 | 4 |
| 분할 거리 | 고정 | PSSM λ=0.75 |

### 3.2 캐스케이드 분할 (NVIDIA PSSM, λ=0.75)

| 캐스케이드 | 뷰 공간 깊이 범위 |
|---|---|
| 0 | 0 ~ 10 m |
| 1 | 10 ~ 50 m |
| 2 | 50 ~ 200 m |
| 3 | 200 ~ 1000 m |

분할 공식: `split[i] = λ × near × (far/near)^(i/N) + (1-λ) × (near + (i/N)×(far-near))`

### 3.3 `ShadowRenderer` 재설계

**파일:** `src/rendering/ShadowRenderer.hpp/.cpp`

```cpp
class ShadowRenderer {
    static constexpr uint32_t NUM_CASCADES = 4;
    static constexpr uint32_t SHADOW_MAP_SIZE = 2048;

    void updateLightMatrices(lightDir, cameraPos, view, proj, near, far);
    rhi::RHIRenderPassEncoder* beginShadowPass(encoder, frameIndex, cascadeIndex);
    void endShadowPass();

    rhi::RHITextureView* getShadowMapView() const;  // View2DArray (전체 4레이어)
    const glm::mat4& getLightSpaceMatrix(uint32_t cascade) const;
    const glm::vec4& getCascadeSplits() const;
};
```

- `m_shadowMap`: `imageType = 2D`, `arrayLayers = 4` depth array texture
- `m_shadowMapView`: `View2DArray` — 조명 셰이더 샘플링용
- `m_cascadeViews[4]`: 각 캐스케이드별 2D 뷰 — 렌더 타겟용
- per-frame × per-cascade UBO/BindGroup: `[2][4]` 2차원 배열
- Linux: 캐스케이드별 `VkFramebuffer[4]` 별도 생성

**Tight Frustum-Fit 직교 행렬 계산:**
카메라 Frustum 슬라이스 8개 코너를 라이트 공간으로 변환 후
AABB를 구해 직교 투영 범위 결정 → 텍셀 낭비 최소화.

### 3.4 `drawFrame` 내 렌더링 순서 변경

```
[Frustum Cull Compute]
    ↓
[Shadow Pass × 4]           ← cascade 0~3 순서로 반복
    ShadowRenderer::beginShadowPass(encoder, frameIndex, cascadeIndex)
    → draw instanced geometry
    ShadowRenderer::endShadowPass()
    ↓
[GBuffer Pass (MRT)]        ← 포워드 Main HDR Pass 대체
    GBufferPass::execute(...)
    ↓
[Deferred Lighting Pass]    ← Render Graph에 편입 (SSAO 이전)
    addPass("DeferredLighting") → addReadDep(GBuf0/1/2, depth), addWriteDep(hdrColor)
    ↓
[SSAO Compute]
    ↓
[Bloom Compute]
    ↓
[PostProcess Pass]
    ↓
[Present]
```

---

## 4. UBO 확장

**파일:** `src/rendering/Renderer.hpp` (UniformBufferObject 구조체)

```cpp
struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 invView;          // ← 신규: 뷰 공간 → 월드 공간 변환
    glm::mat4 invProj;          // ← 신규: 클립 공간 → 뷰 공간 변환 (뎁스 재구성용)
    glm::vec3 sunDirection;
    float     sunIntensity;
    glm::vec3 sunColor;
    float     ambientIntensity;
    glm::vec3 cameraPos;
    float     exposure;
    glm::mat4 lightSpaceMatrices[4];  // ← 신규: 4-cascade 행렬 배열
    glm::vec4 cascadeSplits;          // ← 신규: 뷰 공간 분할 깊이
    glm::vec2 shadowMapSize;
    float     shadowBias;
    float     shadowStrength;
};
```

---

## 5. Render Graph 통합

Deferred Lighting 패스가 Phase 2에서 구축한 Render Graph에 통합됨.

```cpp
// drawFrame() 내 Render Graph 선언부 (추가된 패스)
auto rgGBuf0 = m_renderGraph.importTexture("GBuf0", gbufferPass->getGBuffer0(), ...);
auto rgGBuf1 = m_renderGraph.importTexture("GBuf1", gbufferPass->getGBuffer1(), ...);
auto rgGBuf2 = m_renderGraph.importTexture("GBuf2", gbufferPass->getGBuffer2(), ...);

auto deferredPass = m_renderGraph.addPass("DeferredLighting", RGPassType::Render,
    [this, frameIndex](rhi::RHICommandEncoder* enc) {
        // fullscreen deferred lighting draw
    });
m_renderGraph.addReadDep(deferredPass, rgGBuf0, RGAccess::SampleFragment);
m_renderGraph.addReadDep(deferredPass, rgGBuf1, RGAccess::SampleFragment);
m_renderGraph.addReadDep(deferredPass, rgGBuf2, RGAccess::SampleFragment);
m_renderGraph.addReadDep(deferredPass, rgDepth, RGAccess::SampleFragment);
m_renderGraph.addWriteDep(deferredPass, rgHDR,  RGAccess::ColorWrite);
```

G-Buffer 텍스처 레이아웃 전환 (Undefined → ColorAttachment → ShaderReadOnly) 자동 처리.

---

## 6. 빌드 변경

**CMakeLists.txt:**
- `GBufferPass.cpp`, `DeferredLightingPass.cpp` native 빌드 타겟에 추가
- 신규 셰이더 컴파일 타겟:
  ```
  gbuffer.vert.glsl         → gbuffer.vert.spv
  gbuffer.frag.glsl         → gbuffer.frag.spv
  deferred_lighting.vert.glsl → deferred_lighting.vert.spv
  deferred_lighting.frag.glsl → deferred_lighting.frag.spv
  ```

---

## 7. 최종 렌더링 파이프라인 전체 흐름

```
[Frame Start]
    ↓
[Frustum Cull Compute]         objectBuffer → indirectDrawBuffer
    ↓
[Shadow Pass × 4 (CSM)]        geometry → shadowDepthArray[4] (2048×2048×4)
    ↓
[GBuffer Pass (MRT)]           geometry → GBuf0/1/2 (Normal+R, Albedo+M, AO)
    ↓
─── Render Graph ───────────────────────────────────────────────────────
[Deferred Lighting Pass]       GBuf0/1/2 + depth + CSM + IBL → hdrColor
    ↓
[SSAO Compute]                 depth → ssaoTexture → ssaoBlurTexture (R8Unorm)
    ↓
[Bloom Threshold Compute]      hdrColor → bloomTexture (절반 해상도)
    ↓
[Bloom Blur ×4]                bloomTexture ping-pong
    ↓
[PostProcess Pass]             hdrColor + bloom + ssaoBlur → ACES + FXAA → swapchain
    ↓
[ImGui]
    ↓
[Present]
────────────────────────────────────────────────────────────────────────
```

---

## 8. 로드맵 진행 현황

| Phase | 내용 | 상태 |
|---|---|---|
| Phase 1 | Vulkan Post-Processing (HDR, Bloom, SSAO, FXAA) | ✅ 완료 |
| Phase 2 | Render Graph (자동 sync2 배리어 추론) | ✅ 완료 |
| Phase 3 | Deferred Rendering + 4-Cascade CSM | ✅ 완료 |
| Phase 4 | Bindless Rendering | 미착수 |
| Phase 5 | 센서 시뮬레이션 (LiDAR / 카메라) | 미착수 |

> **참고:** Phase 3 완성으로 CAREER_ROADMAP.md 기준 **MORAI 정식 지원 권장 시점** 도달.

---

## Phase 4 변경 이력 (동일 날짜 추가)

> 작업 범위: Phase 4 — VMA Custom Memory Pools + Bindless Textures (Descriptor Indexing)

---

## 9. 개요 (Phase 4)

Vulkan 메모리 관리 최적화와 Bindless Rendering 기반 시설 구축.

**VMA 스테이징 풀**로 CPU→GPU 업로드 버퍼의 서브-할당 효율을 높이고,
**Per-frame Descriptor Pool**로 프레임 단위 동적 바인드 그룹 리셋 인프라를 완성.
**Bindless Texture Manager**로 단일 디스크립터 셋에 4096개 텍스처를 등록하고
G-Buffer 셰이더에서 인스턴스별 텍스처 인덱스로 재질을 샘플링한다.

---

## 10. Task 4.1 — VMA Custom Memory Pools

### 10.1 VMA Staging Pool

**파일:** `src/rhi/backends/vulkan/src/VulkanRHIDevice.cpp`

```cpp
VmaPoolCreateInfo poolInfo{};
poolInfo.flags        = VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT;  // 순차 할당, 일괄 해제
poolInfo.blockSize    = 16 * 1024 * 1024;  // 16 MB 블록
poolInfo.minBlockCount = 1;
poolInfo.maxBlockCount = 8;
```

- `VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT`: 순차 bump-pointer 방식 할당 → 프레임-스코프 스테이징 버퍼에 적합
- 물리 장치 실제 API 버전으로 초기화 (`physProps.apiVersion` — 하드코딩 1.3 제거)
- `CopySrc | MapWrite` 버퍼는 스테이징 풀에서 우선 할당

### 10.2 Per-frame Descriptor Pools

```cpp
for (int i = 0; i < 2; ++i)
    m_perFrameDescriptorPools[i] = vk::raii::DescriptorPool(m_device, poolInfo);
```

- 2개 풀 (프레임 인플라이트 수와 동일), `eFreeDescriptorSet` 없음
- `resetPerFrameDescriptorPool(frameIndex)`: `vkResetDescriptorPool`으로 풀 전체 리셋
- 동적 바인드 그룹(향후 확장) 전용 — 현재는 인프라 완성 단계

---

## 11. Task 4.2 — Descriptor Indexing / Bindless Textures

### 11.1 런타임 피처 감지

**파일:** `VulkanRHIDevice::createLogicalDevice()`

```
지원 확인 항목:
  - shaderSampledImageArrayNonUniformIndexing
  - descriptorBindingPartiallyBound
  - descriptorBindingSampledImageUpdateAfterBind
  - runtimeDescriptorArray
```

→ 4개 모두 지원 시 `m_hasDescriptorIndexing = true` + 디바이스 생성 pNext 체인에 피처 구조체 추가.
→ lavapipe (Vulkan 1.1) 등 미지원 드라이버: `m_hasDescriptorIndexing = false`, 절차적 albedo 폴백.

### 11.2 RHI 인터페이스 확장

| 파일 | 변경 내용 |
|---|---|
| `RHIBindGroup.hpp` | `BindingType::BindlessTextures` 추가, `BindGroupLayoutEntry::descriptorCount` 필드 추가 |
| `RHIPipeline.hpp` | `PipelineLayoutDesc::nativeExtraSetLayouts: vector<void*>` — 네이티브 VkDescriptorSetLayout 추가용 |
| `VulkanRHIBindGroup.cpp` | `BindlessTextures` → `eCombinedImageSampler`, `descriptorCount` 반영 |
| `VulkanRHIPipeline.cpp` | `nativeExtraSetLayouts`를 `setLayouts` 벡터 뒤에 append |
| `VulkanRHICommandEncoder.hpp/cpp` | `bindNativeDescriptorSet(setIndex, VkDescriptorSet)` 추가 |

### 11.3 BindlessTextureManager

**파일:** `src/rendering/BindlessTextureManager.hpp/.cpp`

```cpp
class BindlessTextureManager {
    static constexpr uint32_t MAX_TEXTURES    = 4096;
    static constexpr uint32_t INVALID_INDEX   = 0xFFFFFFFF;

    bool       isAvailable() const;
    uint32_t   registerTexture(RHITextureView*, RHISampler*) -> slot index;
    void*      getVkDescriptorSetLayout() const;   // VkDescriptorSetLayout
    void*      getVkDescriptorSet()       const;   // VkDescriptorSet
};
```

- `VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT` 풀
- `VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT` 레이아웃
- `VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | UPDATE_AFTER_BIND_BIT` 바인딩
- `registerTexture()`: `vkUpdateDescriptorSets`로 슬롯에 즉시 기록

### 11.4 GBufferPass 통합

- `initialize()`에 `VkDescriptorSetLayout bindlessLayout` 파라미터 추가
- 지원 시: 파이프라인 레이아웃에 set 2로 bindlessLayout 추가
- 미지원 시: `gbuffer_nobindless.frag.spv` 로드 (RuntimeDescriptorArray 기능 없음)
- `execute()`에 `VkDescriptorSet bindlessSet` 파라미터 추가
  → `vulkanPass->bindNativeDescriptorSet(2, bindlessSet)` 호출

### 11.5 gbuffer 셰이더 쌍

| 파일 | 용도 |
|---|---|
| `shaders/gbuffer.frag.glsl` | 바인들리스 지원 드라이버용 — `GL_EXT_nonuniform_qualifier`, `sampler2D allTextures[]`, `nonuniformEXT()` 사용 |
| `shaders/gbuffer_nobindless.frag.glsl` | 폴백 — 런타임 배열 없음, 절차적 albedo only |
| `shaders/gbuffer.vert.glsl` | `floatBitsToUint(roughnessAOPad.b)` → `fragAlbedoIndex` 출력 |

**GBuffer vertex 셰이더 인스턴스별 텍스처 인덱스:**

```glsl
fragAlbedoIndex = floatBitsToUint(obj.roughnessAOPad.b);
// 0xFFFFFFFF = 텍스처 없음 → procedural albedo 폴백
```

### 11.6 BuildingManager 텍스처 인덱스 인코딩

**파일:** `src/game/managers/BuildingManager.cpp`

```cpp
// setUseBindlessTextures(true) 호출 시에만 실제 인덱스 사용
ground.roughnessAOPad.b = m_useBindlessTextures
    ? glm::uintBitsToFloat(0u)             // concrete (slot 0)
    : glm::uintBitsToFloat(0xFFFFFFFFu);   // INVALID (폴백)

uint32_t matIdx = (scale.y > 60.0f) ? 2u : 1u;
obj.roughnessAOPad.b = m_useBindlessTextures
    ? glm::uintBitsToFloat(matIdx)         // metal=1, glass=2
    : glm::uintBitsToFloat(0xFFFFFFFFu);
```

- `BuildingManager::setUseBindlessTextures(bool)` 신규 메서드
- `Application::initGameLogic()`에서 `renderer->isBindlessAvailable()` 확인 후 호출

### 11.7 Renderer 통합

**파일:** `src/rendering/Renderer.cpp` (`createBindlessResources()`)

```
1×1 RGBA8 재질 텍스처 3종 생성:
  Slot 0: concrete  (89, 89, 97)   — 아스팔트
  Slot 1: metal     (127, 133, 140) — 일반 건물
  Slot 2: glass     (102, 148, 184) — 고층 건물 (height > 60m)

스테이징 버퍼 → copyBufferToTexture → transitionLayout → 등록
```

---

## 12. lavapipe 호환성 수정

| 문제 | 원인 | 수정 |
|---|---|---|
| `vmaFindMemoryTypeIndexForBufferInfo` SIGSEGV | VMA `vulkanApiVersion=1.3` but device=1.1 → 1.2+ 함수 포인터 null | `physProps.apiVersion`으로 실제 버전 사용 |
| `vkCmdPipelineBarrier2` assertion abort | lavapipe가 `VK_KHR_synchronization2` 미지원 | `BarrierBatch::flush()` sync2 없으면 `pipelineBarrier` 폴백 |
| GBuffer pipeline SIGSEGV in lavapipe | `gbuffer.frag.spv`가 `RuntimeDescriptorArray` capability 요구 | bindless 비활성 시 `gbuffer_nobindless.frag.spv` 로드 |

---

## 13. 빌드 변경 (Phase 4)

**CMakeLists.txt:**

- `BindlessTextureManager.cpp/.hpp` → MiniEngine 소스에 추가
- `gbuffer_nobindless.frag.glsl → gbuffer_nobindless.frag.spv` 컴파일 타겟 추가

**Makefile:**

- `make shaders` 타겟 추가: `cmake --build $(BUILD_DIR) --target building_shaders`

---

## 14. 로드맵 진행 현황 (갱신)

| Phase | 내용 | 상태 |
|---|---|---|
| Phase 1 | Vulkan Post-Processing (HDR, Bloom, SSAO, FXAA) | ✅ 완료 |
| Phase 2 | Render Graph (자동 sync2 배리어 추론) | ✅ 완료 |
| Phase 3 | Deferred Rendering + 4-Cascade CSM | ✅ 완료 |
| Phase 4 | VMA Custom Pools + Bindless Textures | ✅ 완료 |
| Phase 5 | 센서 시뮬레이션 (LiDAR / 카메라) | 미착수 |

> **런타임 검증:** lavapipe (Vulkan 1.1) 에서 모든 기능이 graceful fallback으로 동작 확인.
> 바인들리스는 `VK_EXT_descriptor_indexing` 지원 드라이버 (RTX 3060 등)에서 활성화됨.
