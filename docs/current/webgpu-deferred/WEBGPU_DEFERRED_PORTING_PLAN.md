# WebGPU Deferred Rendering Porting Plan

**Version**: 1.0  
**Date**: 2026-05-01  
**Status**: Planning  
**Goal**: Vulkan showcase_demo와 동등한 시각적 결과물을 WebGPU/WASM에서 구현

---

## 목차

- [현황 분석](#현황-분석)
- [핵심 전략 결정](#핵심-전략-결정)
- [Phase 0 — Push Constant Emulator](#phase-0--push-constant-emulator)
- [Phase 1 — G-Buffer WGSL + GBufferPass 활성화](#phase-1--g-buffer-wgsl--gbufferpass-활성화)
- [Phase 2 — Deferred Lighting WGSL + DeferredLightingPass 활성화](#phase-2--deferred-lighting-wgsl--deferredlightingpass-활성화)
- [Phase 3 — Bloom (렌더패스 방식)](#phase-3--bloom-렌더패스-방식)
- [Phase 4 — SSAO (컴퓨트)](#phase-4--ssao-컴퓨트)
- [Phase 5 — 통합 PostProcess Pass](#phase-5--통합-postprocess-pass)
- [Phase 6 — 가드 정리 & 통합 프레임 루프](#phase-6--가드-정리--통합-프레임-루프)
- [포팅하지 않는 항목](#포팅하지-않는-항목)
- [일정](#일정)

---

## 현황 분석

### 기능별 WebGPU 지원 현황

| 기능 | Vulkan | WebGPU | 비고 |
|---|---|---|---|
| Shadow (4-Cascade CSM) | ✅ | ✅ WGSL 있음 | `shadow.wgsl` |
| IBL (irradiance/prefilter/BRDF LUT) | ✅ | ✅ WGSL 있음 | IBL compute shaders |
| Bloom | ✅ | ✅ WGSL 있음 | `bloom_prefilter.wgsl`, `bloom_blur.wgsl` |
| Tonemap (ACES) + FXAA | ✅ | ✅ WGSL 있음 | 별도 2패스로 분리 |
| G-Buffer | ✅ | ❌ WGSL 없음 | `gbuffer.{vert,frag}.glsl`만 존재 |
| Deferred Lighting (PBR) | ✅ | ❌ WGSL 없음 | `deferred_lighting.{vert,frag}.glsl`만 존재 |
| SSAO + blur | ✅ | ❌ WGSL 없음 | `ssao.comp.glsl`만 존재 |
| Bindless Textures | ✅ | ❌ WebGPU 미지원 | `binding_array` 브라우저 지원 부재 |
| GPU Profiler (타임스탬프) | ✅ | ❌ 옵션 기능 | `timestamp-query` 선택적 |

### 구조적 강점 (이미 갖춰진 것)

- **RHI 인터페이스**: 100% 백엔드 무관. Vulkan/WebGPU 구현체가 동일한 인터페이스 구현
- **ShadowRenderer**: 완전히 RHI-agnostic, WebGPU에서 즉시 동작
- **GBufferPass / DeferredLightingPass**: `#ifndef __EMSCRIPTEN__` 가드만 제거하면 RHI 코드 재사용 가능
- **Renderer.hpp 멤버**: `rhi::RHI*` 타입 사용, Vulkan 고유 타입 없음

### 핵심 블로커

**Push Constants**: WebGPU는 push constants 미지원. `WebGPURHIRenderPassEncoder::setPushConstants()`는 현재 no-op stub. 아래 4개 셰이더가 push constants에 의존:

| 셰이더 | 데이터 | 크기 |
|---|---|---|
| `postprocess.frag.glsl` | texelSize(2), bloomStr, exposure, aoStr, tonemapOn, debugView, fxaaOn | 32 bytes |
| `ssao.comp.glsl` | mat4 projection + 6 floats | 96 bytes |
| `ssao_blur.comp.glsl` | 6 floats | 24 bytes |
| `bloom_threshold.comp.glsl` | invSize(2), threshold, knee | 16 bytes |
| `bloom_blur.comp.glsl` | invSize(2), iteration, pad | 16 bytes |

---

## 핵심 전략 결정

### Deferred Rendering 유지 (Forward 전환 안 함)

WebGPU는 MRT(Multiple Render Targets)를 완전히 지원. GBufferPass/DeferredLightingPass가 이미 RHI 타입만 사용하므로 WGSL 셰이더 추가 + `#ifdef` 내부 분기만으로 활성화 가능. Forward로 전환하면 전체 PBR + CSM + IBL + 포인트 라이트 로직을 새 WGSL에 중복 작성해야 하므로 비효율적.

### Push Constants → 소형 UBO 에뮬레이션

프레임당 256바이트 정렬 UBO 1개 + 마지막 BindGroup 슬롯 주입. `queue.writeBuffer()` 비용은 무시할 수준. Vulkan 경로는 기존 push constant 경로 완전히 유지, 에뮬레이터는 `#ifdef __EMSCRIPTEN__`에서만 인스턴스화.

### Bloom: 기존 WGSL 렌더패스 재사용

`bloom_prefilter.wgsl` + `bloom_blur.wgsl`이 이미 존재. Dual Kawase compute 대신 3패스 Gaussian 렌더패스 사용. 화질 차이 미미, push constants 없이 동작.

### SSAO 스토리지 텍스처: `rgba8unorm`

`r8unorm-storage`는 WebGPU 옵션 기능으로 브라우저 지원 불확실. `rgba8unorm` 스토리지 텍스처로 대체(메모리 4배지만 하프 해상도이므로 실용적으로 무시 가능). `.r` 채널만 읽고 씀.

### Bindless: 포팅 안 함

`gbuffer_nobindless.frag.glsl` 폴백이 이미 존재하며 시각적으로 동등. WebGPU `binding_array`는 브라우저 지원 부재.

---

## Phase 0 — Push Constant Emulator

**복잡도:** S | **선행 조건:** 없음 (모든 Phase의 기반)

### 목적

이후 모든 Phase에서 WebGPU 셰이더의 push constant 파라미터를 주입하는 공통 메커니즘 제공.

### 신규 파일

`src/rendering/PushConstantEmulator.hpp/.cpp` (~80줄)

```cpp
class PushConstantEmulator {
public:
    // 초기화: MAX_FRAMES_IN_FLIGHT개의 256바이트 UBO + BindGroupLayout 생성
    void initialize(rhi::RHIDevice* device, uint32_t frameCount);

    // 매 프레임 호출: data를 현재 프레임 UBO에 업로드
    void update(rhi::RHICommandEncoder* encoder, uint32_t frameIdx,
                const void* data, size_t size);

    // drawFrame에서 마지막 BindGroup 슬롯에 setBindGroup으로 주입
    rhi::RHIBindGroup* getBindGroup(uint32_t frameIdx) const;
    rhi::RHIBindGroupLayout* getLayout() const;
};
```

**사용 패턴 (WebGPU 경로에서만):**

```cpp
#ifdef __EMSCRIPTEN__
PostProcessParams params { bloomStrength, exposure, aoStrength, ... };
pushConstEmulator.update(encoder, frameIdx, &params, sizeof(params));
renderEncoder->setBindGroup(PUSH_CONST_SLOT, pushConstEmulator.getBindGroup(frameIdx));
#else
renderEncoder->setPushConstants(layout, stages, 0, sizeof(params), &params);
#endif
```

### 수정 파일

| 파일 | 변경 내용 |
|---|---|
| `src/rendering/PushConstantEmulator.hpp` | 신규 |
| `src/rendering/PushConstantEmulator.cpp` | 신규 |
| `src/rendering/Renderer.hpp` | `#ifdef __EMSCRIPTEN__` 멤버 추가 |

---

## Phase 1 — G-Buffer WGSL + GBufferPass 활성화

**복잡도:** M | **선행 조건:** Phase 0

### 신규 셰이더

`shaders/gbuffer.wgsl` — vert + frag, MRT 3채널 출력

```
Bind Group 0: UBO (UniformBufferObject — Vulkan과 동일 레이아웃)
Bind Group 1: ObjectBuffer SSBO + visible indices

MRT Output:
  target 0: normal.xyz + roughness (RGBA16Float)
  target 1: albedo.rgb + metallic  (RGBA8Unorm)
  target 2: AO                     (R8Unorm)
```

`gbuffer_nobindless.frag.glsl` 기준으로 번역 (절차적 albedo, bindless 없음).

**UBO 레이아웃 동기화 필요**: 현재 `building.wgsl`의 UBO가 `lightSpaceMatrix` 1개만 보유. Deferred lighting에서 `invView`, `invProj`, `lightSpaceMatrices[4]`, `cascadeSplits`가 필요하므로 WGSL UBO 구조체를 C++ `UniformBufferObject`와 완전히 일치시켜야 함.

### 수정 파일

| 파일 | 변경 내용 |
|---|---|
| `shaders/gbuffer.wgsl` | 신규 |
| `shaders/building.wgsl` | UBO 구조체에 `invView`, `invProj`, `lightSpaceMatrices[4]`, `cascadeSplits` 추가 |
| `src/rendering/GBufferPass.hpp` | 외부 `#ifndef __EMSCRIPTEN__` 가드 제거, 내부 셰이더 로딩 분기 추가 |
| `src/rendering/GBufferPass.cpp` | WGSL 셰이더 로딩 분기, `VkDescriptorSetLayout` 파라미터 EMSCRIPTEN에서 nullptr |
| `src/rendering/Renderer.hpp` | GBufferPass 멤버 가드 제거 |
| `src/rendering/Renderer.cpp` | `createGBufferPass()` 가드 제거, `drawFrame()` WebGPU 경로에서 호출 |

---

## Phase 2 — Deferred Lighting WGSL + DeferredLightingPass 활성화

**복잡도:** M | **선행 조건:** Phase 1

### 신규 셰이더

`shaders/deferred_lighting.wgsl` — vert(풀스크린 삼각형) + frag(PBR + CSM + IBL)

```
Bind Group 0 (12 bindings):
  0: UBO
  1: gBuffer0 (normal+roughness)   texture_2d<f32>
  2: gBuffer1 (albedo+metallic)    texture_2d<f32>
  3: gBuffer2 (AO)                 texture_2d<f32>
  4: depthTex                      texture_depth_2d
  5: gbufferSampler                sampler
  6: shadowCsmArray                texture_depth_2d_array
  7: shadowSampler                 sampler_comparison
  8: irradianceMap                 texture_cube<f32>
  9: prefilteredMap                texture_cube<f32>
 10: brdfLUT                       texture_2d<f32>
 11: iblSampler                    sampler
```

**CSM 샘플링 (WGSL):**

```wgsl
// GLSL texture2DArray → WGSL textureSampleCompare with array_index
let shadow = textureSampleCompare(shadowCsmArray, shadowSampler,
                                  uv, cascadeIndex, currentDepth - bias);
```

**WorldPos 재구성 (depth → view → world):**

```wgsl
// NDC는 Vulkan과 동일 (+Y up, depth [0,1])
let ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
let viewPos = ubo.invProj * ndc;
let worldPos = ubo.invView * (viewPos / viewPos.w);
```

Push constants 없음 — 모든 파라미터가 UBO에 존재.

### 수정 파일

| 파일 | 변경 내용 |
|---|---|
| `shaders/deferred_lighting.wgsl` | 신규 |
| `src/rendering/DeferredLightingPass.hpp` | 외부 가드 제거 |
| `src/rendering/DeferredLightingPass.cpp` | WGSL 분기, `nativeRenderPass` → nullptr (EMSCRIPTEN) |
| `src/rendering/Renderer.hpp` | DeferredLightingPass 멤버 가드 제거 |
| `src/rendering/Renderer.cpp` | `createDeferredLightingPass()` 가드 제거, `drawFrame()` WebGPU 경로 연결 |

---

## Phase 3 — Bloom (렌더패스 방식)

**복잡도:** M | **선행 조건:** Phase 2 | **병렬 가능:** Phase 4와 동시 진행

### 전략

Vulkan의 Dual Kawase Compute 대신 이미 존재하는 `bloom_prefilter.wgsl` + `bloom_blur.wgsl` 렌더패스 방식 사용. 3패스 Gaussian 블러 (Prefilter → H-blur → V-blur).

### 신규 리소스 (WebGPU 전용)

```
bloomTexture     — half-res RGBA16Float (RenderTarget | Sampled)
bloomPingTexture — half-res RGBA16Float (RenderTarget | Sampled)
bloomPrefilterPipeline  — bloom_prefilter.wgsl
bloomBlurHPipeline      — bloom_blur.wgsl (horizontal)
bloomBlurVPipeline      — bloom_blur.wgsl (vertical)
```

텍스처 크기(`textureDimensions()` WGSL 내장 함수)로 step size 계산 → push constants 불필요.

### 수정 파일

| 파일 | 변경 내용 |
|---|---|
| `shaders/bloom_blur.wgsl` | horizontal/vertical 분기를 uniform binding 또는 entry point로 분리 |
| `src/rendering/Renderer.hpp` | `#ifdef __EMSCRIPTEN__` bloom 멤버 추가 |
| `src/rendering/Renderer.cpp` | `createBloomPipeline_WASM()`, `drawFrame()` bloom 패스 추가 |

---

## Phase 4 — SSAO (컴퓨트)

**복잡도:** M | **선행 조건:** Phase 2 | **병렬 가능:** Phase 3와 동시 진행

### 신규 셰이더

**`shaders/ssao.comp.wgsl`** — `ssao.comp.glsl` 번역

```
Bind Group 0:
  0: depthTexture   texture_depth_2d
  1: noiseTex       texture_2d<f32>
  2: outputTex      texture_storage_2d<rgba8unorm, write>  (r8 대신)
  3: params UBO     { projection, invProjection, ssaoKernel[64], radius, bias, ... }
```

**`shaders/ssao_blur.comp.wgsl`** — `ssao_blur.comp.glsl` 번역

```
Bind Group 0:
  0: inputTex   texture_2d<f32>
  1: outputTex  texture_storage_2d<rgba8unorm, write>
  2: params UBO { texelSize, ... }
```

**스토리지 텍스처 포맷 결정:**

```cpp
// r8unorm-storage 지원 여부 런타임 확인
if (device->getCapabilities().r8UnormStorage) {
    ssaoFormat = rhi::TextureFormat::R8Unorm;   // 최적
} else {
    ssaoFormat = rhi::TextureFormat::RGBA8Unorm; // 범용 폴백
}
```

### 수정 파일

| 파일 | 변경 내용 |
|---|---|
| `shaders/ssao.comp.wgsl` | 신규 |
| `shaders/ssao_blur.comp.wgsl` | 신규 |
| `src/rhi/include/rhi/RHICapabilities.hpp` | `r8UnormStorage` 필드 추가 |
| `src/rhi/backends/webgpu/src/WebGPURHICapabilities.cpp` | `r8unorm-storage` feature 쿼리 |
| `src/rendering/Renderer.hpp` | `#ifdef __EMSCRIPTEN__` SSAO 멤버 추가 |
| `src/rendering/Renderer.cpp` | `createSSAOPipeline_WASM()`, `drawFrame()` SSAO 디스패치 추가 |

---

## Phase 5 — 통합 PostProcess Pass

**복잡도:** S | **선행 조건:** Phase 3, Phase 4

### 목적

현재 WebGPU의 2단계(tonemap → FXAA)를 단일 패스로 통합. Vulkan `postprocess.frag.glsl`과 동등한 기능.

### 신규 셰이더

`shaders/postprocess.wgsl` — 기존 `tonemap.wgsl` + `fxaa.wgsl` 대체

```
Bind Group 0:
  0: hdrTexture   texture_2d<f32>
  1: bloomTexture texture_2d<f32>
  2: ssaoTexture  texture_2d<f32>
  3: sampler      sampler
  4: params UBO   { bloomStr, exposure, aoStr, debugView, fxaaOn, tonemapOn, texelW, texelH }
```

처리 순서:
1. AO 합성: `hdr *= ssao.r`
2. Bloom 합성: `hdr += bloom * bloomStr`
3. ACES Filmic Tonemap (조건부)
4. FXAA (조건부)
5. Swapchain에 직접 출력

### 제거 대상 (Renderer.hpp)

```cpp
// 삭제
std::unique_ptr<rhi::RHIRenderPipeline> tonemapPipeline;   // #ifdef __EMSCRIPTEN__
std::unique_ptr<rhi::RHIRenderPipeline> fxaaPipeline;      // #ifdef __EMSCRIPTEN__
std::unique_ptr<rhi::RHITexture> ldrColorTexture;           // 중간 버퍼 불필요
```

### 수정 파일

| 파일 | 변경 내용 |
|---|---|
| `shaders/postprocess.wgsl` | 신규 (tonemap.wgsl, fxaa.wgsl 대체) |
| `src/rendering/Renderer.hpp` | tonemap/fxaa 파이프라인, ldrColorTexture 제거, postprocess 추가 |
| `src/rendering/Renderer.cpp` | `createPostProcessPipeline_WASM()`, PushConstantEmulator 연결 |

---

## Phase 6 — 가드 정리 & 통합 프레임 루프

**복잡도:** S | **선행 조건:** Phase 1~5 완료

### drawFrame() 통합 구조

```
[현재 WebGPU]                     [목표 (공통 흐름)]
──────────────────────────────    ──────────────────────────────
Shadow Pass                  →    Shadow Pass
(G-Buffer 없음)              →    G-Buffer Pass          ← Phase 1
(Deferred 없음)              →    Deferred Lighting Pass ← Phase 2
(SSAO 없음)                  →    SSAO Dispatch          ← Phase 4
(Bloom 없음)                 →    Bloom Pass             ← Phase 3
tonemap pass (ACES)          →    PostProcess Pass       ← Phase 5
fxaa pass                    →    (통합됨)
present                      →    present
```

Vulkan은 RenderGraph 경로 그대로 유지. WebGPU는 flat sequential 인코더 패스.

### 정리 대상

- `Renderer.hpp` / `.cpp`: GBufferPass, DeferredLightingPass 멤버의 외부 `#ifndef __EMSCRIPTEN__` 가드 제거
- `GBufferPass.hpp/.cpp`, `DeferredLightingPass.hpp/.cpp`: 외부 가드 제거, 내부 플랫폼 분기는 유지
- `BindlessTextureManager`: Vulkan 전용 가드 **유지** (WebGPU 동등 기능 없음)

---

## 포팅하지 않는 항목

| 항목 | 이유 |
|---|---|
| **Bindless Textures** | WebGPU `binding_array` 브라우저 지원 부재. `gbuffer_nobindless` 폴백으로 시각적 동등성 확보 |
| **RenderGraph** | WebGPU 암묵적 리소스 트래킹으로 배리어 불필요. flat sequential 패스가 올바른 모델 |
| **GpuProfiler** | `timestamp-query` 옵션 기능, 비주얼 패리티와 무관 |
| **비동기 컴퓨트** | WebGPU 단일 큐 모델. `computeTimelineSemaphore`는 이미 capability 체크로 분기 |
| **ImGui** | 현재 Vulkan 전용, WASM 데모 범위 외 |
| **Dual Kawase Bloom** | 기존 WGSL Gaussian 블러로 시각적으로 동등 |

---

## 일정

| Phase | 내용 | 복잡도 | 예상 기간 |
|---|---|---|---|
| 0 | Push Constant Emulator | S | 1일 |
| 1 | G-Buffer WGSL + GBufferPass 활성화 | M | 3~4일 |
| 2 | Deferred Lighting WGSL + DeferredLightingPass 활성화 | M | 3~4일 |
| 3 | Bloom 렌더패스 (Phase 4와 병렬) | M | 2~3일 |
| 4 | SSAO 컴퓨트 (Phase 3와 병렬) | M | 2~3일 |
| 5 | 통합 PostProcess Pass | S | 1~2일 |
| 6 | 가드 정리 & 프레임 루프 통합 | S | 1일 |
| | **합계** | | **약 3~4주** |

```
Week 1:  Phase 0 → Phase 1
Week 2:  Phase 2
Week 3:  Phase 3 + Phase 4 (병렬)
Week 4:  Phase 5 → Phase 6
```
