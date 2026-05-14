# 변경 이력 — 2026-04-08

> 작업 범위: Phase 1 완성 (SSAO + 버그 수정) / Phase 2 Render Graph 아키텍처 / 창 크기 조정 / ImGui UI 확장

---

## 1. 창 크기 기본값 변경

**파일:** `src/Application.hpp`

| 항목 | 이전 | 이후 |
|---|---|---|
| `WINDOW_WIDTH` | 800 | 1280 |
| `WINDOW_HEIGHT` | 600 | 720 |

---

## 2. SSAO (Screen-Space Ambient Occlusion) 구현 — Task 1.4

### 2.1 신규 셰이더

#### `shaders/ssao.comp.glsl`
- 뎁스 버퍼로부터 뷰 공간 위치 재구성
- 인터리브드 그래디언트 노이즈 회전 기반 TBN 프레임 생성
- 8-sample 반구 샘플링으로 주변 차폐 계산
- AO 출력: `R8Unorm` 스토리지 이미지

**Push Constants (96 bytes):**
```c
mat4  projection;  // 뷰→클립 행렬
float radius;      // 뷰 공간 커널 반경 (0.5)
float bias;        // self-intersection 방지 (0.025)
float near, far;   // 뎁스 선형화 범위
float invW, invH;  // 절반 해상도 역수 (픽셀→UV)
float pad[2];
```

**바인딩:**
| 바인딩 | 타입 | 설명 |
|---|---|---|
| 0 | `SampledTexture` | 뎁스 텍스처 (D32Sfloat) |
| 1 | `StorageTexture` | AO 출력 (R8Unorm) |
| 2 | `Sampler` | 공용 샘플러 |

#### `shaders/ssao_blur.comp.glsl`
- Bilateral 5×5 블러 (깊이 에지 보존)
- 공간 Gaussian 가중치 × 깊이 유사도 가중치

**바인딩:**
| 바인딩 | 타입 | 설명 |
|---|---|---|
| 0 | `SampledTexture` | raw SSAO 텍스처 |
| 1 | `SampledTexture` | 뎁스 텍스처 (에지 보존용) |
| 2 | `StorageTexture` | 블러된 AO 출력 |
| 3 | `Sampler` | 공용 샘플러 |

### 2.2 postprocess.frag.glsl 업데이트

SSAO 바인딩 추가 및 AO 적용 로직 추가:

```glsl
layout(set = 0, binding = 3) uniform texture2D ssaoTexture;

// push_constant 추가
float aoStrength;  // [0, 1] SSAO 어두워지는 강도

// tonemappedSample() 내부
float ao = texture(sampler2D(ssaoTexture, hdrSampler), uv).r;
composite *= mix(1.0, ao, params.aoStrength);
```

### 2.3 CMakeLists.txt — 셰이더 컴파일 타겟 추가

```cmake
# ssao.comp.glsl → ssao.comp.spv
# ssao_blur.comp.glsl → ssao_blur.comp.spv
```

### 2.4 Renderer.hpp 변경

**신규 멤버 (Vulkan 전용):**
```cpp
// SSAO 텍스처 (절반 해상도 R8Unorm)
std::unique_ptr<rhi::RHITexture>     ssaoTexture, ssaoBlurTexture;
std::unique_ptr<rhi::RHITextureView> ssaoTextureView, ssaoBlurView;
std::unique_ptr<rhi::RHISampler>     ssaoSampler;

// SSAO 컴퓨트 파이프라인
std::unique_ptr<rhi::RHIShader>           ssaoShader, ssaoBlurShader;
std::unique_ptr<rhi::RHIBindGroupLayout>  ssaoLayout, ssaoBlurLayout;
std::unique_ptr<rhi::RHIBindGroup>        ssaoBindGroup, ssaoBlurBindGroup;
std::unique_ptr<rhi::RHIPipelineLayout>   ssaoPipelineLayout, ssaoBlurPipelineLayout;
std::unique_ptr<rhi::RHIComputePipeline>  ssaoPipeline, ssaoBlurPipeline;

// 포스트 프로세스 파라미터
float bloomStrength = 0.04f;
float aoStrength    = 0.6f;
```

**신규 public setter/getter:**
```cpp
void  setBloomStrength(float s); float getBloomStrength() const;
void  setAOStrength(float s);    float getAOStrength()    const;
```

**신규 private 메서드:**
```cpp
void createSSAOPipeline();
```

### 2.5 Renderer.cpp — 초기화 순서

**수정 전 (버그 있음):**
```
createPostProcessPipeline()  // ssaoBlurView가 아직 null → 잘못된 bind group
createBloomPipeline()
createSSAOPipeline()
```

**수정 후:**
```
createBloomPipeline()        // bloomTextureView 생성
createSSAOPipeline()         // ssaoBlurView 생성
createPostProcessPipeline()  // 모든 뷰가 준비된 후 bind group 생성
```

### 2.6 Renderer.cpp — drawFrame 렌더링 순서

```
[Main HDR Pass 완료]
    ↓
[SSAO Compute — 절반 해상도]
    depth: DepthStencilAttachment → ShaderReadOnly (전환)
    ssaoTexture: Undefined → General (write)
    ssaoBlurTexture: Undefined → General (write)
    → SSAO pass (8 samples, half-res dispatch)
    ssaoTexture: General → ShaderReadOnly
    → SSAO Blur pass (bilateral 5×5)
    ssaoBlurTexture: General → ShaderReadOnly
    ↓
[Bloom Compute]
    ↓
[PostProcess Pass]
    HDR + Bloom + SSAO → ACES Tonemap + FXAA → swapchain
```

---

## 3. 버그 수정

### 3.1 SSAO 법선 방향 버그 (바닥 검은색 현상 원인)

**파일:** `shaders/ssao.comp.glsl`

```glsl
// 수정 전 (버그): 카메라를 향한 법선을 반대로 뒤집음
if (normal.z > 0.0) normal = -normal;

// 수정 후: 카메라 반대 방향 법선을 바로잡음
if (normal.z < 0.0) normal = -normal;
```

**영향:** 뷰 공간에서 카메라 방향(+z)을 향하는 법선이 반대로 뒤집혀서 반구 샘플이 표면 아래로 들어가 전체 화면이 AO=0으로 렌더링됨.

### 3.2 샘플 벡터 z 성분 양수 보장

**파일:** `shaders/ssao.comp.glsl`

```glsl
// 수정 전: z가 음수인 샘플 포함 → 하반구 샘플링
vec3( 0.5381,  0.1856, -0.4319), ...

// 수정 후: 모든 z 성분 양수 → 상반구(카메라 방향) 보장
vec3( 0.5381,  0.1856,  0.4319), ...
```

### 3.3 depth 텍스처 생성 플래그 (바닥 검은색 원인 2)

**파일:** `src/rendering/Renderer.cpp` — `createRHIDepthResources()`

```cpp
// 수정 전 (버그): 샘플링 불가, lazy 메모리
depthDesc.usage = rhi::TextureUsage::DepthStencil;
depthDesc.transient = true;

// 수정 후: SSAO 컴퓨트 셰이더에서 샘플링 가능
depthDesc.usage = rhi::TextureUsage::DepthStencil | rhi::TextureUsage::Sampled;
depthDesc.transient = false;
```

### 3.4 HDR 렌더패스 depth storeOp (바닥 검은색 원인 3)

**파일:** `src/rhi/backends/vulkan/src/VulkanRHISwapchain.cpp` — `createHDRRenderPass()`

```cpp
// 수정 전 (버그): 렌더패스 후 depth 내용 undefined
depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;

// 수정 후: SSAO가 읽을 수 있도록 보존
depthAttachment.storeOp = vk::AttachmentStoreOp::eStore;
```

### 3.5 `transitionTextureLayout` depth aspect mask 자동 감지

**파일:** `src/rhi/backends/vulkan/src/VulkanRHICommandEncoder.cpp`

```cpp
// 수정 전 (버그): 모든 텍스처에 eColor 사용
barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;

// 수정 후: 포맷으로 depth 여부 판별
bool isDepth = (fmt == Depth32Float || fmt == Depth16Unorm || ...);
barrier.subresourceRange.aspectMask = isDepth ? eDepth : eColor;
```

### 3.6 `transitionTextureLayout` DepthStencilAttachment 단계 추가

**파일:** `src/rhi/backends/vulkan/src/VulkanRHICommandEncoder.cpp`

```cpp
// srcStage 추가
case rhi::TextureLayout::DepthStencilAttachment:
    srcStage  = vk::PipelineStageFlagBits::eLateFragmentTests;
    srcAccess = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    break;

// dstStage 추가
case rhi::TextureLayout::DepthStencilAttachment:
    dstStage  = vk::PipelineStageFlagBits::eEarlyFragmentTests;
    dstAccess = eDepthStencilAttachmentRead | eDepthStencilAttachmentWrite;
    break;
```

---

## 4. SSAO 성능 최적화 (lavapipe 소프트웨어 렌더러)

| 항목 | 변경 | 효과 |
|---|---|---|
| 해상도 | 전체(1280×720) → **절반(640×360)** | 픽셀 4× 감소 |
| 샘플 수 | 16 → **8** | 연산량 2× 감소 |
| 총 SSAO 작업량 | — | **약 8× 감소** |
| 기본 aoStrength | 0.8 → **0.6** | 과도한 어두움 방지 |

---

## 5. ImGui UI 확장

**파일:** `src/ui/ImGuiManager.hpp`, `src/ui/ImGuiManager.cpp`

`LightingSettings` 구조체에 포스트 프로세스 파라미터 추가:

```cpp
float bloomStrength = 0.04f;  // Bloom 강도 (0~0.5)
float aoStrength    = 0.6f;   // SSAO 강도 (0~1.0)
```

UI에 슬라이더 추가 (Post-Processing 섹션):
- **Bloom Strength**: 0.000 ~ 0.500
- **AO Strength**: 0.00 ~ 1.00

**파일:** `src/Application.cpp`

ImGui 설정값을 Renderer에 전달:
```cpp
renderer->setBloomStrength(lighting.bloomStrength);
renderer->setAOStrength(lighting.aoStrength);
```

---

## 6. 최종 Vulkan 렌더링 파이프라인 전체 흐름

```
[Frame Start]
    ↓
[Frustum Cull Compute]        objectBuffer → indirectDrawBuffer
    ↓
[Shadow Pass]                 geometry → shadowDepthTexture
    ↓
[Main HDR Pass]               geometry → hdrColorTexture(RGBA16Float) + depthTexture(D32Sfloat)
    ↓
[SSAO Compute]                depth(절반해상도) → ssaoTexture → ssaoBlurTexture (R8Unorm)
    ↓
[Bloom Threshold Compute]     hdrColorTexture → bloomTexture (RGBA16Float, 절반해상도)
    ↓
[Bloom Blur ×4]               bloomTexture ↔ bloomPingTexture (ping-pong)
    ↓
[PostProcess Pass]            hdrColor + bloom + ssaoBlur → ACES + SSAO + FXAA → swapchain
    ↓
[ImGui]                       PostProcess 패스 내 렌더링
    ↓
[Present]
```

---

## 7. Phase 2 — Render Graph 아키텍처

### 7.1 개요

`drawFrame()` 내 하드코딩된 패스 순서와 수동 `vkCmdPipelineBarrier` / `transitionTextureLayout` 호출을 선언적 Render Graph로 대체.

**제거된 수동 배리어 코드 (~230줄):**

| 기존 코드 | 제거 방법 |
|---|---|
| Swapchain UNDEFINED → ColorAttachment (50줄) | `importSwapchainImage` + `addWriteDep(ColorWrite)` |
| Swapchain ColorAttachment → PresentSrc (50줄) | `addReadDep(PresentSrc)` Present 패스 |
| Shadow DepthStencil ↔ ShaderReadOnly (40줄) | `addWriteDep(DepthWrite)` / `addReadDep(SampleFragment)` |
| Depth DepthStencil → ShaderReadOnly (6줄) | `addReadDep(SampleCompute)` SSAO 패스 |
| SSAO Undefined → General → ShaderReadOnly (6줄) | `addWriteDep(StorageWrite)` + `addReadDep(SampleCompute)` |
| Bloom ping-pong 8+ 전환 (~80줄) | BloomBlur 패스별 `addReadDep`/`addWriteDep` |

### 7.2 신규 파일

```
src/rendering/graph/
├── RenderGraphResource.hpp   — 리소스 핸들, RGAccess enum, RGTexState/RGBufState
├── RenderPass.hpp            — RGPassNode, RGPassType, RGDep
├── BarrierBatch.hpp          — vkCmdPipelineBarrier2 배치 방출 (sync2)
├── RenderGraph.hpp           — 선언적 API 인터페이스
└── RenderGraph.cpp           — Kahn's 위상 정렬 + 배리어 자동 추론 구현
```

### 7.3 RenderGraph 핵심 API

```cpp
// 매 프레임 drawFrame() 내에서
m_renderGraph.reset();

// 리소스 등록 (초기 상태와 함께)
auto rgHDR      = m_renderGraph.importTexture("HDR",   hdrColorTexture.get(), hdrInitial);
auto rgBloom    = m_renderGraph.importTexture("Bloom", bloomTexture.get(),    undefinedSt);
auto rgSwapchain = m_renderGraph.importSwapchainImage("Swapchain", vkImage, ...);

// 패스 선언 (실행 콜백 + 읽기/쓰기 의존성)
auto ssaoPass = m_renderGraph.addPass("SSAO", RGPassType::Compute,
    [this, sW, sH](rhi::RHICommandEncoder* enc) { /* compute dispatch */ });
m_renderGraph.addReadDep(ssaoPass, rgDepth, RGAccess::SampleCompute);   // depth → ShaderReadOnly
m_renderGraph.addWriteDep(ssaoPass, rgSSAO, RGAccess::StorageWrite);    // ssao → General

m_renderGraph.compile();           // 위상 정렬 (Kahn's algorithm)
m_renderGraph.execute(encoder.get()); // sync2 배리어 자동 방출 + 패스 실행
```

### 7.4 배리어 추론 방식

`inferTexState(RGAccess)` — 액세스 타입에서 `(VkPipelineStageFlags2, VkAccessFlags2, VkImageLayout)` 자동 계산:

| RGAccess | Stage | Access | Layout |
|---|---|---|---|
| ColorWrite | COLOR_ATTACHMENT_OUTPUT | COLOR_ATTACHMENT_WRITE | COLOR_ATTACHMENT_OPTIMAL |
| DepthWrite | EARLY/LATE_FRAGMENT_TESTS | DEPTH_STENCIL_ATTACHMENT_RW | DEPTH_STENCIL_ATTACHMENT_OPTIMAL |
| SampleFragment | FRAGMENT_SHADER | SHADER_SAMPLED_READ | SHADER_READ_ONLY_OPTIMAL |
| SampleCompute | COMPUTE_SHADER | SHADER_SAMPLED_READ | SHADER_READ_ONLY_OPTIMAL |
| StorageWrite | COMPUTE_SHADER | SHADER_STORAGE_WRITE | GENERAL |
| PresentSrc | NONE | NONE | PRESENT_SRC_KHR |

배리어는 현재 상태와 필요 상태가 다를 때만 생성 → `BarrierBatch`에 누적 후 패스 직전 `vkCmdPipelineBarrier2` 단일 호출.

### 7.5 위상 정렬 (compile)

Kahn's algorithm: 리소스 쓰기 패스 → 읽기 패스 간 방향성 간선 구성 후 in-degree 0인 패스부터 처리. 사이클 감지 시 선언 순서(fallback) 유지.

### 7.6 플랫폼별 처리

| 플랫폼 | Shadow 배리어 | Swapchain 배리어 | SSAO/Bloom 배리어 |
|---|---|---|---|
| macOS/Windows | render graph | render graph | render graph |
| Linux | native render pass 자동 처리 | native render pass 자동 처리 | render graph |

Linux는 `rgSwapchain`을 import하지 않음 (`#if !defined(__linux__)` 조건).

### 7.7 수정된 기존 파일

- **`src/rendering/Renderer.hpp`** — `rendergraph::RenderGraph m_renderGraph` 멤버 추가, `RenderGraph.hpp` include
- **`src/rendering/Renderer.cpp`** — `drawFrame()` 내 SSAO·Bloom·PostProcess·Present 섹션을 render graph 선언으로 교체
- **`CMakeLists.txt`** — `src/rendering/graph/` 소스 파일 native 빌드 타겟에 추가
- **`src/rhi/include/rhi/RHICommandBuffer.hpp`** — `class RHIPipelineLayout` forward declaration 추가

### 7.8 최종 drawFrame 구조

```
[Frustum Cull] — 기존 코드 (자체 배리어 포함)
[Shadow Pass]  — 기존 코드 (기존 플랫폼별 배리어 포함)
[Main HDR Pass] — 기존 코드
────────────────────────────────────────
m_renderGraph.reset();
  importTexture(HDR, depth, ssao, bloom, swapchain)
  addPass(SSAO)        → addReadDep(depth), addWriteDep(ssao)
  addPass(SSAOBlur)    → addReadDep(ssao), addWriteDep(ssaoBlur)
  addPass(BloomThreshold) → addReadDep(HDR), addWriteDep(bloom)
  addPass(BloomBlur×4) → ping-pong readDep/writeDep
  addPass(PostProcess) → addReadDep(HDR, bloom, ssaoBlur), addWriteDep(swapchain)
  addPass(Present)     → addReadDep(swapchain, PresentSrc)  [!linux]
m_renderGraph.compile();   // 위상 정렬
m_renderGraph.execute();   // sync2 배리어 자동 방출 + 실행
```

---

## 8. 빌드 및 실행

```bash
make build     # 빌드 + 셰이더 컴파일
make run-only  # 실행 (lavapipe 소프트웨어 렌더러)
```

> **참고:** WSL2 환경에서 NVIDIA/AMD/Intel 하드웨어 Vulkan ICD가 동작하지 않아  
> lavapipe(Mesa 소프트웨어 렌더러)를 사용 중. 하드웨어 GPU는 브라우저 WebGPU 경로에서만 활성화됨.
