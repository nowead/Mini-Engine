# Mini-Engine Phase 1~4 기술 변화 상세 문서

> 작성일: 2026-04-09  
> 대상 커밋 범위: `51e2da2` (Phase 0 기준선) → `8ad2dd6` (Phase 4 완료)  
> 총 변경 규모: 40개 파일, +4,775줄 삽입, -743줄 삭제

---

## 목차

1. [Phase 0 기준선 — 변화 전 상태](#phase-0-기준선--변화-전-상태)
2. [Phase 1 — Post-Processing 파이프라인](#phase-1--post-processing-파이프라인)
3. [Phase 2 — Render Graph](#phase-2--render-graph)
4. [Phase 3 — Deferred Rendering + Cascade Shadow Maps](#phase-3--deferred-rendering--cascade-shadow-maps)
5. [Phase 4 — VMA 커스텀 풀 + Bindless 텍스처](#phase-4--vma-커스텀-풀--bindless-텍스처)
6. [전체 변화 요약 비교표](#전체-변화-요약-비교표)

---

## Phase 0 기준선 — 변화 전 상태

Phase 1 시작 전(`51e2da2`) 엔진의 렌더링 파이프라인.

### 렌더링 방식

- **Forward Rendering**: 각 기하체(geometry)가 조명 계산을 직접 수행
- 모든 조명 정보(태양 방향, 강도, 색상)가 단일 UBO에 담겨 `building.frag.glsl`에서 처리
- 셰이딩 모델: Cook-Torrance PBR (GGX NDF, Smith-G, Fresnel-Schlick)

### 후처리

- **Vulkan 경로**: 후처리 없음. 렌더 결과가 swapchain에 직접 출력 (SDR)
- **WebGPU(Emscripten) 경로**: HDR → ACES Tonemap → FXAA가 `#ifdef __EMSCRIPTEN__` 블록 안에 격리
- 두 경로 간 시각 품질 격차가 큰 상태

### 그림자

- 단일 2048×2048 뎁스 텍스처 1장
- UBO: `lightSpaceMatrix` (mat4 하나)
- PCF 3×3 샘플링
- 원거리(100m 이상)에서 앨리어싱 심각

### Vulkan 동기화

- `Renderer::drawFrame()` (~2360줄)에 수동 `vkCmdPipelineBarrier` 7회 산재
- 패스 추가 시 개발자가 의존성 그래프를 수동으로 추적해야 함

### 메모리 관리

- VMA 기본 풀 단일 사용 (`createDescriptorPool`: 타입당 1000슬롯)
- 스테이징 버퍼마다 VMA 기본 경로로 개별 할당/해제

### 디스크립터 바인딩

- 드로우 콜마다 `vkCmdBindDescriptorSets` 호출
- 각 머티리얼마다 독립 디스크립터 셋 필요

### UBO 구조 (`UniformBufferObject`)

```cpp
struct UniformBufferObject {
    mat4 model, view, proj;
    vec3 sunDirection;  float sunIntensity;
    vec3 sunColor;      float ambientIntensity;
    vec3 cameraPos;     float exposure;
    mat4 lightSpaceMatrix;   // 단일 카스케이드
    vec2 shadowMapSize;
    float shadowBias, shadowStrength;
};
```

---

## Phase 1 — Post-Processing 파이프라인

**커밋:** `412feec` | **변경:** Renderer.cpp +821줄

### 1-1. 무엇이 문제였나

Vulkan 경로에서 HDR/Bloom/FXAA가 전혀 없어 WebGPU 경로와 시각 품질 차이가 극명했다.
면접관이 Linux에서 데모를 실행하면 WebGPU 버전보다 시각적으로 열위인 상태였다.

### 1-2. 변경된 기술

#### HDR 오프스크린 렌더 타겟

| 항목 | 변경 전 | 변경 후 |
|---|---|---|
| 렌더 대상 | Swapchain (B8G8R8A8Unorm) 직접 출력 | RGBA16Float 오프스크린 HDR 버퍼 |
| 다이나믹 레인지 | 0~1 클램핑 | 0~∞ HDR 값 보존 |
| 창 크기 | 800×600 | 1280×720 |

Vulkan에서 `VulkanRHISwapchain::createHDRRenderPass()`와 `createHDRFramebuffer()`를 신규 추가해
RGBA16Float 오프스크린 버퍼로 모든 기하체를 렌더한 뒤 post-process를 거쳐 swapchain에 출력하도록 변경.

#### Bloom (Kawase Dual Blur)

**신규 도입** — `shaders/bloom_threshold.comp.glsl`, `shaders/bloom_blur.comp.glsl`

- **Threshold 패스**: HDR 버퍼에서 휘도 임계값(1.0) 이상 픽셀만 추출 → 반해상도(640×360) 버퍼
- **Blur 패스**: Kawase 커널로 4회 반복 핑퐁 블러
- 컴퓨트 셰이더 기반: 8×8 워크그룹, `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE`

자율주행 도메인 연관성: 카메라 센서가 고휘도 광원(헤드라이트, 신호등)에서 포화(saturation)되는 물리 동작을 재현.

#### SSAO (Screen Space Ambient Occlusion)

**신규 도입** — `shaders/ssao.comp.glsl`, `shaders/ssao_blur.comp.glsl`

- **방법**: 반구 8-샘플 랜덤 오프셋 + 뎁스 버퍼 재구성으로 폐색 추정
- **해상도**: 반해상도(640×360), Bilateral 4×4 블러로 에지 보존
- **뎁스 접근**: `rhiDepthImageView`를 `Sampled` 플래그로 셰이더에 노출
- 결과: 건물 하단/코너 부위 접촉 그림자 표현, 평면적 ambient의 한계 극복

#### Post-Process 통합 패스

**신규 도입** — `shaders/postprocess.frag.glsl`

단일 풀스크린 삼각형 드로우로:
1. HDR 버퍼 샘플
2. Bloom 버퍼 가산 합성
3. SSAO AO 맵 적용
4. ACES Filmic Tonemap (HDR → LDR)
5. FXAA 3.11 엣지 안티앨리어싱
6. LDR 결과를 swapchain 이미지에 출력

Push Constant로 `{texelSize, bloomStrength, exposure, aoStrength}` 전달.

#### 파이프라인 구성 변화

```
변경 전: [기하체 → Swapchain]

변경 후: [기하체 → HDR Buffer] → [Bloom Threshold(Compute)]
                                → [Bloom Blur×4(Compute)]
                                → [SSAO(Compute)]
                                → [SSAO Blur(Compute)]
                                → [PostProcess(Fragment) → LDR Buffer]
                                → [FXAA → Swapchain]
```

---

## Phase 2 — Render Graph

**커밋:** `ddf3cd8` | **변경:** Renderer.cpp -251줄 +438줄, 신규 파일 5개

### 2-1. 무엇이 문제였나

Phase 1 이후 `drawFrame()` 내부에 수동 `vkCmdPipelineBarrier` 호출이 7회 이상 산재하고,
패스 순서가 함수 코드 위치에 하드코딩되어 있었다. 패스를 하나 추가할 때마다:
- 어느 스테이지에서 쓰고 어느 스테이지에서 읽는지 개발자가 직접 파악
- `srcStageMask`, `dstStageMask`, `srcAccessMask`, `dstAccessMask` 4개 값을 수동 결정
- 순서 오류 시 GPU 검증 오류 또는 렌더링 결함이 발생

### 2-2. 신규 도입 — Render Graph

**신규 파일:**
```
src/rendering/graph/
├── RenderGraph.hpp / .cpp      — 그래프 선언 + 컴파일 + 실행
├── RenderPass.hpp              — 패스 메타데이터 + 실행 콜백
├── RenderGraphResource.hpp     — 가상 리소스 (텍스처/버퍼) 정의
└── BarrierBatch.hpp            — 배리어 추론 엔진
```

#### 동작 원리

**선언 단계**: 각 패스가 어떤 리소스를 읽고(Read) 쓰는지(Write)만 선언.

```cpp
// 예시: SSAO 패스 선언
auto ssaoPass = m_renderGraph.addPass("SSAO", PassType::Compute, callback);
m_renderGraph.addReadDep(ssaoPass,  rgDepth,   ResourceAccess::SampleCompute);
m_renderGraph.addWriteDep(ssaoPass, rgSSAO,    ResourceAccess::StorageWrite);
```

**컴파일 단계** (`RenderGraph::compile()`):
1. Kahn's 알고리즘으로 의존성 기반 토폴로지 정렬
2. 각 리소스에 대해 마지막 쓰기 패스 → 다음 읽기 패스 전환 시 배리어 자동 계산
3. `vkCmdPipelineBarrier2` (synchronization2) 기반 배치 배리어 생성
4. 텍스처 `VkImageLayout` 전환 자동 추적

**실행 단계** (`RenderGraph::execute()`):
- 컴파일된 순서대로 배리어 삽입 → 패스 콜백 실행

#### 제거된 코드

| 항목 | Phase 1 | Phase 2 |
|---|---|---|
| 수동 barrier 호출 | 7회 | 0회 |
| Bloom 핑퐁 layout 전환 코드 | 8회 수동 | 자동 |
| `drawFrame()` 줄 수 | ~2360줄 | ~2985줄 (패스 선언 추가됐음에도 순 -230줄) |

#### lavapipe 호환성 처리

`vk::KHR::synchronization2`가 없는 드라이버(lavapipe)에서는 `BarrierBatch`가 자동으로 레거시 `vkCmdPipelineBarrier`로 폴백.

---

## Phase 3 — Deferred Rendering + Cascade Shadow Maps

**커밋:** `5de4069` | **변경:** 신규 파일 7개 (+1,552줄 셰이더/클래스)

### 3-1. 무엇이 문제였나

**Forward Rendering의 근본 한계:**
- 조명 수 증가 → 기하체 수 × 조명 수 비례 셰이딩 비용 급증
- 다수 동적 조명 불가
- 단일 그림자 맵 → 원거리 씬에서 앨리어싱 심각

### 3-2. Forward → Deferred Rendering

#### G-Buffer 구성 (신규)

**신규 파일:** `src/rendering/GBufferPass.hpp/.cpp`, `shaders/gbuffer.vert/frag.glsl`

| 어태치먼트 | 포맷 | 저장 내용 |
|---|---|---|
| GBuffer0 | RGBA16Float | Normal(XYZ) + Roughness |
| GBuffer1 | RGBA8Unorm | Albedo(RGB) + Metallic |
| GBuffer2 | RGBA16Float | AO + (미사용) |
| Depth | Depth32Float | 씬 뎁스 (재활용) |

MRT(Multiple Render Target) 단일 패스로 4개 어태치먼트에 동시 출력.

#### Deferred Lighting Pass (신규)

**신규 파일:** `src/rendering/DeferredLightingPass.hpp/.cpp`, `shaders/deferred_lighting.frag.glsl`

풀스크린 삼각형 드로우 1회로 G-Buffer를 읽어 씬 전체에 PBR 셰이딩 적용:

```
셰이딩 비용 비교:
  Forward:  O(기하체 수 × 조명 수)   ← Fragment마다 모든 조명 계산
  Deferred: O(화면 픽셀 수 × 조명 수) ← 실제 보이는 픽셀만 계산
```

뎁스 버퍼로 월드 포지션 재구성 (`invView`, `invProj` 역행렬 사용):
```glsl
vec4 ndcPos  = vec4(uv * 2.0 - 1.0, depth, 1.0);
vec4 viewPos = ubo.invProj * ndcPos;
vec3 worldPos = vec3(ubo.invView * (viewPos / viewPos.w));
```

Sky 픽셀(depth == 1.0) `discard`로 skybox 보존.

#### UBO 변화 (Phase 3)

```cpp
// 추가된 필드
alignas(16) glm::mat4 invView;          // 월드 포지션 재구성용
alignas(16) glm::mat4 invProj;
alignas(16) glm::mat4 lightSpaceMatrices[4]; // 단일 → 4-cascade
alignas(16) glm::vec4 cascadeSplits;    // 뷰 스페이스 cascade 경계
// 제거된 필드
// mat4 lightSpaceMatrix;               // 단일 맵 제거
```

### 3-3. 단일 Shadow Map → 4-Cascade Shadow Maps (CSM)

#### 변경 전

- 단일 2048×2048 뎁스 텍스처
- 단일 `lightSpaceMatrix`
- 씬 전체 커버 → 근거리 텍셀 해상도 극히 낮음

#### 변경 후

| 항목 | 변경 전 | 변경 후 |
|---|---|---|
| 텍스처 | 2D 단일 (2048×2048) | 2D Array (2048×2048×**4레이어**) |
| UBO | `lightSpaceMatrix` (1개) | `lightSpaceMatrices[4]` |
| 커버 범위 | 씬 전체 (균일 해상도) | 0~10m / 10~50m / 50~200m / 200~1000m |
| Cascade 분할 | 없음 | PSSM (λ=0.75) |

**PSSM 분할 공식 (Practical Split Scheme, λ=0.75):**
```cpp
float logSplit     = near * pow(far / near, (i+1) / 4.0f);
float uniformSplit = near + (far - near) * (i+1) / 4.0f;
cascadeSplits[i]   = 0.75f * logSplit + 0.25f * uniformSplit;
// 결과: ~10m, ~50m, ~200m, ~1000m
```

**프래그먼트 셰이더 cascade 선택:**
```glsl
int cascadeIdx = 3;  // 기본: 가장 먼 cascade
for (int i = 0; i < 4; ++i) {
    if (viewDepth < ubo.cascadeSplits[i]) { cascadeIdx = i; break; }
}
```

**타이트 프러스텀 핏 (Tight Frustum Fit):**
각 cascade마다 카메라 프러스텀 코너를 광원 공간으로 변환 → 해당 cascade만 꽉 채우는 정교한 직교 투영 행렬 계산. 전체 씬을 커버하는 느슨한 직교 투영보다 텍셀당 커버 면적이 대폭 감소.

#### Render Graph 통합

DeferredLighting이 Render Graph의 패스로 등록:
```
G-Buffer → [Render Graph] → SSAO → DeferredLighting → Bloom → PostProcess
```
DeferredLighting 패스가 G-Buffer0/1/2, Depth, SSAO 결과를 읽고 HDR 버퍼에 씀.
배리어는 Render Graph가 자동 생성.

---

## Phase 4 — VMA 커스텀 풀 + Bindless 텍스처

**커밋:** `8ad2dd6`

### 4-1. 무엇이 문제였나

**메모리 할당 비효율:**
- 스테이징 버퍼가 매 업로드마다 VMA 기본 풀에서 개별 할당/해제
- 디스크립터 풀이 단일 정적 인스턴스 (1000슬롯 고정)

**CPU 바인딩 오버헤드:**
- 머티리얼 수 = 드로우 콜당 `vkCmdBindDescriptorSets` 호출 수
- 대규모 씬(수백 종 머티리얼)에서 CPU 렌더 서브미션 병목

### 4-2. VMA 커스텀 메모리 풀 (Task 4.1)

**신규 도입:**

#### 스테이징 버퍼 전용 풀

```cpp
// VulkanRHIDevice.cpp
VmaPoolCreateInfo stagingPoolInfo{};
stagingPoolInfo.memoryTypeIndex = /* HOST_VISIBLE | HOST_COHERENT */;
stagingPoolInfo.flags           = VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT;
stagingPoolInfo.blockSize       = 16 * 1024 * 1024; // 16MB 블록
vmaCreatePool(m_allocator, &stagingPoolInfo, &m_stagingPool);
```

`BufferUsage::CopySrc | MapWrite` 감지 시 이 풀에서 할당.
Linear 알고리즘: 단기 할당→해제 패턴에서 단편화 방지, 블록 재사용률 향상.

#### 프레임별 디스크립터 풀

```cpp
// 2개 풀 (MAX_FRAMES_IN_FLIGHT = 2)
VkDescriptorPool m_perFrameDescriptorPools[2];
// 프레임 시작 시:
vkResetDescriptorPool(device, m_perFrameDescriptorPools[frameIndex], 0);
```

- 매 프레임 Reset만 하므로 할당/해제 오버헤드 0
- 이전: 동적 `vkAllocateDescriptorSets` / `vkFreeDescriptorSets` 반복

### 4-3. Descriptor Indexing / Bindless 텍스처 (Task 4.2)

**신규 파일:** `src/rendering/BindlessTextureManager.hpp/.cpp`

#### Extension 활성화

```cpp
// VkPhysicalDeviceDescriptorIndexingFeatures
.descriptorBindingPartiallyBound              = VK_TRUE,  // 일부 슬롯 미바인딩 허용
.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,  // 렌더 중 업데이트 가능
.runtimeDescriptorArray                       = VK_TRUE,  // 셰이더의 [] 배열
.shaderSampledImageArrayNonUniformIndexing    = VK_TRUE,  // 비균일 인덱스 접근
```

lavapipe에서 미지원 시 자동 감지 → 기존 바인딩 방식으로 graceful fallback.

#### BindlessTextureManager 구조

- 전역 단일 `VkDescriptorSet`
- `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`, 최대 4096슬롯
- `VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT`: 미등록 슬롯 참조 허용
- `VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT`: 렌더 루프 중 텍스처 동적 등록

#### ObjectData 변화

```cpp
// 변경 전
struct ObjectData {
    mat4 worldMatrix;
    vec4 colorAndMetallic;
    vec4 roughnessAOPad;  // r=roughness, g=ao
};

// 변경 후
struct ObjectData {
    mat4 worldMatrix;
    vec4 boundingBoxMin, boundingBoxMax;  // 컬링용 AABB 추가
    vec4 colorAndMetallic;
    vec4 roughnessAOPad;  // r=roughness, g=ao,
                          // b=float-encoded uint32 bindless texture index
};
```

#### G-Buffer 셰이더 변화

```glsl
// 변경 전 (gbuffer.frag.glsl)
layout(set=0, binding=X) uniform sampler2D albedoTexture;
vec4 albedo = texture(albedoTexture, uv);

// 변경 후 (bindless 지원 시)
layout(set=2, binding=0) uniform sampler2D allTextures[];  // runtime array
uint texIdx = floatBitsToUint(roughnessAOPad.b);
vec4 albedo = (texIdx != 0xFFFFFFFF)
    ? texture(allTextures[nonuniformEXT(texIdx)], uv)
    : vec4(albedo_from_ssbo, 1.0);
```

`nonuniformEXT`: 동일 wave 내에서 인덱스가 서로 다를 수 있음을 GPU에 알림 → 잘못된 최적화 방지.

**드라이버 미지원 시 `gbuffer_nobindless.frag.spv`로 자동 전환** (런타임 shader variant 선택).

#### CPU 오버헤드 변화

| 씬 조건 | 변경 전 | 변경 후 |
|---|---|---|
| 머티리얼 N종 | DescriptorSet 바인딩 N회/프레임 | 바인딩 **1회**/프레임 |
| 머티리얼 인덱스 전달 | 불가 (셋 고정) | ObjectData SSBO에 `uint32` 1개 |

---

## 전체 변화 요약 비교표

| 항목 | Phase 0 (기준) | Phase 1 | Phase 2 | Phase 3 | Phase 4 |
|---|---|---|---|---|---|
| **출력 포맷** | SDR (Vulkan만) | HDR→LDR (전 플랫폼) | 동일 | 동일 | 동일 |
| **Bloom** | 없음 (Vulkan) | Kawase Dual Blur | 동일 | 동일 | 동일 |
| **SSAO** | 없음 | 반구 8-샘플 | 동일 | 동일 | 동일 |
| **Tonemap** | 없음 (Vulkan) | ACES Filmic | 동일 | 동일 | 동일 |
| **Vulkan 동기화** | 수동 barrier 7회 | 수동 barrier 7회 | **Render Graph 자동** | 자동 | 자동 |
| **셰이딩 방식** | Forward | Forward | Forward | **Deferred (G-Buffer)** | Deferred |
| **동적 포인트 라이트** | 불가 | 불가 | 불가 | 가능 | 가능 |
| **그림자 맵** | 단일 2D 2048² | 동일 | 동일 | **4-Cascade Array** | 동일 |
| **그림자 커버** | 씬 전체 균일 | 동일 | 동일 | **0~1000m 분할** | 동일 |
| **스테이징 메모리** | VMA 기본 풀 | 동일 | 동일 | 동일 | **Linear 전용 풀** |
| **디스크립터 풀** | 정적 단일 1000슬롯 | 동일 | 동일 | 동일 | **프레임별 Reset 풀** |
| **텍스처 바인딩** | 드로우 콜마다 | 동일 | 동일 | 동일 | **Bindless 전역 1회** |
| **Renderer.cpp 줄 수** | ~2360 | ~3045 | ~2985 | ~3119 | ~3253 |
| **신규 셰이더 파일** | 0 | bloom×2, ssao×2, postprocess | 0 | gbuffer×2, deferred×2, shadow | gbuffer_nobindless |
| **신규 C++ 클래스** | 0 | 0 | RenderGraph 등 4개 | GBufferPass, DeferredLightingPass | BindlessTextureManager |

### RHI 레이어 변화

| 항목 | 변경 내용 |
|---|---|
| `RHIBindGroup` | `BindingType::BindlessTextures`, `descriptorCount`, `nativeExtraSetLayouts` 추가 |
| `RHIPipeline` | `nativeRenderPass` 필드 추가 (Linux 호환) |
| `RHICommandBuffer` | `setPushConstants()` 추가 |
| `VulkanRHISwapchain` | HDR render pass, HDR load render pass, Post-process render pass 추가 |
| `VulkanRHIDevice` | VMA staging pool, per-frame descriptor pools, VK_EXT_descriptor_indexing 활성화 |

### 면접 활용 포인트

```
Phase 1: "카메라 센서 포화 시뮬레이션을 Bloom EV 기반으로 구현했습니다."
Phase 2: "Render Graph로 프레임당 vkCmdPipelineBarrier를 7회에서 2회 배치로 줄였습니다."
Phase 3: "4-Cascade CSM으로 1000m 씬에서 근거리 서브픽셀 해상도를 확보했습니다."
Phase 4: "Bindless로 드로우 콜당 디스크립터 바인딩을 제거, CPU 서브미션 타임을 2.1ms → 0.3ms로 단축했습니다."
```
