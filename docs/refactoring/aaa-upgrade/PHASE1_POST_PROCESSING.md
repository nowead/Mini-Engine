# Phase 1 — Vulkan Post-Processing Pipeline

> 작성일: 2026-04-08  
> 상태: **완료**  
> 참고: [CAREER_ROADMAP.md](../../CAREER_ROADMAP.md)

---

## 개요

기존에 `#ifdef __EMSCRIPTEN__` 안에 격리되어 있던 HDR 파이프라인(Tonemap + FXAA)을  
Vulkan 경로에 동등하게 적용하고, Bloom 컴퓨트 패스와 SSAO를 추가했다.

**목표 달성:** Linux Vulkan 데모가 WebAssembly WebGPU 데모와 동일한 시각적 품질을 가진다.

---

## 구현 내용

### Task 1.1 — HDR 오프스크린 렌더 타겟

| 항목 | 내용 |
|---|---|
| 포맷 | `RGBA16Float` (WebGPU 경로와 동일) |
| 해상도 | 스왑체인과 동일 (1280×720 기본값) |
| 용도 | 지오메트리 패스 색상 출력 (swapchain 직접 쓰기 대체) |
| 깊이 버퍼 | `Depth32Float`, 별도 관리 (`rhiDepthImage`) |

**Vulkan 전용 추가 사항:**
- `VulkanRHISwapchain`에 HDR 렌더 패스(`RGBA16Float` + `D32Sfloat`) 추가
- `VulkanRHISwapchain`에 PostProcess 렌더 패스 (색상만, 깊이 없음) 추가
- 프레임버퍼를 각각 분리하여 ImGui를 PostProcess 패스에서 렌더링

---

### Task 1.2 — 포스트 프로세스 셰이더

**신규 파일:**

| 파일 | 설명 |
|---|---|
| `shaders/postprocess.frag.glsl` | ACES Filmic Tonemap + Bloom 합성 + SSAO 적용 + FXAA 3.11 (단일 패스) |
| `shaders/tonemap.vert.glsl` | 풀스크린 트라이앵글 버텍스 셰이더 (기존 재활용) |

**파이프라인 바인딩:**

| 바인딩 | 타입 | 내용 |
|---|---|---|
| 0 | `SampledTexture` | HDR 색상 텍스처 (`RGBA16Float`) |
| 1 | `SampledTexture` | Bloom 텍스처 (`RGBA16Float`, half-res) |
| 2 | `Sampler` | 공용 샘플러 |
| 3 | `SampledTexture` | SSAO 텍스처 (`R8Unorm`, full-res, 블러됨) |

**Push Constants (Fragment):**

```c
struct PostProcessPC {
    float texelW;       // 1.0 / width
    float texelH;       // 1.0 / height
    float bloomStrength; // [0, 1]
    float exposure;      // EV 조정 값
    float aoStrength;    // SSAO 어두워지는 강도 [0, 1]
};
```

---

### Task 1.3 — Bloom 컴퓨트 패스

**알고리즘:** Dual Kawase Blur (4회 반복 ping-pong, 짝수 → 결과가 bloomTexture에 유지)

**신규 파일:**

| 파일 | 설명 |
|---|---|
| `shaders/bloom_threshold.comp.glsl` | HDR → half-res 밝은 픽셀 추출 (임계값 이상) |
| `shaders/bloom_blur.comp.glsl` | Dual Kawase 블러 (ping-pong 4회) |

**파이프라인:**

```
HDR Texture (ShaderReadOnly)
    ↓  [Bloom Threshold] → bloomTexture (General → ShaderReadOnly)
    ↓  [Bloom Blur ×4 ping-pong]
    ↓  bloomTexture (ShaderReadOnly) → PostProcess
```

**Push Constants (Compute):**

```c
// Threshold
struct BloomThresholdPC { float invW; float invH; float threshold; float knee; };

// Blur (per-iteration)
struct BloomBlurPC { float invW; float invH; int iter; float pad; };
```

**물리적 근거:**  
자율주행 카메라 센서의 고휘도 광원 포화(saturation) 동작을 근사.  
`threshold = 1.0` (EV 0 기준 휘도 1.0 이상 픽셀만 Bloom 발생).

---

### Task 1.4 — SSAO (Screen-Space Ambient Occlusion)

**알고리즘:** 반구 샘플링 16개 + 인터리브드 그래디언트 노이즈 회전 + Bilateral 블러

**신규 파일:**

| 파일 | 설명 |
|---|---|
| `shaders/ssao.comp.glsl` | 뎁스 버퍼 → AO 텍스처 (16-sample hemisphere) |
| `shaders/ssao_blur.comp.glsl` | Bilateral 4×4 블러 (깊이 에지 보존) |

**파이프라인:**

```
Depth (DepthStencilAttachment → ShaderReadOnly)
    ↓  [SSAO Compute] → ssaoTexture (General → ShaderReadOnly)
    ↓  [SSAO Blur]    → ssaoBlurTexture (General → ShaderReadOnly)
    ↓  PostProcess 바인딩 3번으로 입력
Depth (ShaderReadOnly → DepthStencilAttachment)  // 다음 프레임 복구
```

**SSAO 파라미터:**

| 파라미터 | 값 | 설명 |
|---|---|---|
| `radius` | 0.5 | 뷰 공간 커널 반경 |
| `bias` | 0.025 | self-intersection 방지 오프셋 |
| `near` / `far` | 0.1 / 1000.0 | 뎁스 선형화 범위 |
| `aoStrength` | 0.8 | PostProcess에서 AO 어두워지는 강도 |

**Push Constants (SSAO Compute, 96 bytes):**

```c
struct SSAOPC {
    mat4  projection;  // 뷰→클립 행렬 (64 bytes)
    float radius;
    float bias;
    float near;
    float far;
    float invW;
    float invH;
    float pad[2];
};  // 64 + 32 = 96 bytes (Vulkan 최소 보장 128 bytes 이내)
```

---

## RHI 추상화 계층 확장

Phase 1 과정에서 RHI 계층에 다음 기능이 추가되었다:

### Push Constants 지원

**`src/rhi/include/rhi/RHIPipeline.hpp`:**

```cpp
struct PushConstantRange {
    ShaderStage stageFlags = ShaderStage::Vertex;
    uint32_t offset = 0;
    uint32_t size = 0;
};

struct PipelineLayoutDesc {
    std::vector<RHIBindGroupLayout*> bindGroupLayouts;
    std::vector<PushConstantRange> pushConstantRanges;  // 추가
    const char* label = nullptr;
};
```

**`src/rhi/include/rhi/RHICommandBuffer.hpp`:**

```cpp
// RHIRenderPassEncoder + RHIComputePassEncoder 양쪽에 추가
virtual void setPushConstants(RHIPipelineLayout* layout, ShaderStage stages,
                              uint32_t offset, uint32_t size, const void* data) = 0;
```

### 이미지 레이아웃 전환 개선

`VulkanRHICommandEncoder::transitionTextureLayout()`에 다음이 추가되었다:

- **Depth 텍스처 aspect mask 자동 감지**: `texture->getFormat()`으로 Depth 포맷 판별, `eDepth` aspect 사용
- **`DepthStencilAttachment` 단계 처리**: `eLateFragmentTests` / `eEarlyFragmentTests` 스테이지 정확히 지정
- **컴퓨트 셰이더 단계 처리**: `General` ↔ `ShaderReadOnly` 전환 시 `eComputeShader` 포함

---

## 렌더링 파이프라인 전체 흐름

```
[Frame Start]
    ↓
[Frustum Cull Compute]       — objectBuffer → indirectDrawBuffer
    ↓
[Shadow Pass]                — geometry → shadowDepthTexture
    ↓
[Main HDR Pass]              — geometry → hdrColorTexture + rhiDepthImage
    (포맷: RGBA16Float + D32Sfloat)
    ↓
[SSAO Compute]               — depth → ssaoTexture → ssaoBlurTexture
    ↓
[Bloom Threshold Compute]    — hdrColorTexture → bloomTexture (half-res)
    ↓
[Bloom Blur ×4]              — bloomTexture ↔ bloomPingTexture (ping-pong)
    ↓
[PostProcess Pass]           — hdrColorTexture + bloomTexture + ssaoBlurTexture
    (포맷: swapchain BGRA8Unorm)  → ACES Tonemap + SSAO + FXAA → swapchain
    ↓
[ImGui]                      — PostProcess 패스 내 렌더링 (동일 포맷)
    ↓
[Present]
```

---

## 빌드 및 실행

```bash
make build     # 빌드 (셰이더 GLSL→SPIR-V 포함)
make run-only  # 실행 (VK_ICD_FILENAMES 등 환경 변수 자동 설정)
```

**창 크기:** 1280×720 (기본값, `src/Application.hpp`의 `WINDOW_WIDTH`/`WINDOW_HEIGHT`로 변경)

---

## 면접 활용 포인트

**MORAI 예상 질문:** "카메라 센서의 빛 포화 현상을 어떻게 표현합니까?"

> "HDR 파이프라인에서 Bloom 임계값을 EV 0 기준으로 설정합니다. 휘도 1.0 이상의 픽셀만 Bloom이 발생하며, Dual Kawase Blur로 반해상도 버퍼에서 4회 반복 처리합니다. 이는 실제 차량 카메라 센서가 고휘도 광원에서 포화되는 물리적 동작과 동일한 모델입니다."

**MORAI 예상 질문:** "SSAO가 자율주행 시뮬레이션에서 왜 유용합니까?"

> "자율주행 카메라 센서 시뮬레이션에서 SSAO는 건물 아랫부분, 도로 경계석, 차량 하부처럼 간접광이 차단되는 영역을 정확히 어둡게 표현합니다. 뎁스 버퍼에서 뷰 공간 위치를 재구성하고 16개 반구 샘플로 주변 차폐를 추정한 뒤, Bilateral 블러로 깊이 에지를 보존하며 노이즈를 제거합니다."

---

## 다음 단계

→ **Phase 2: Render Graph** — `drawFrame()`의 하드코딩된 패스 순서와 수동 배리어를  
Render Graph로 대체하여 아키텍처 설계 역량을 면접에서 증명할 수 있게 한다.
