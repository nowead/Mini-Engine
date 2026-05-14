# Deferred Rendering WebGPU 포팅 트러블슈팅

Vulkan 기반 Deferred Rendering 파이프라인(GBuffer + Deferred Lighting + SSAO)을 WebGPU/WASM으로 포팅하는 과정에서 발생한 빌드 오류 및 브라우저 런타임 오류를 기록합니다.

---

## 목차

- [1. GBufferPass / DeferredLightingPass 빌드 오류 — `#ifndef __EMSCRIPTEN__` 가드](#1-gbufferpass--deferredlightingpass-빌드-오류--ifndef-__emscripten__-가드)
- [2. WebGPU 깊이 텍스처 바인딩 타입 불일치 — `SampledTexture` vs `DepthTexture`](#2-webgpu-깊이-텍스처-바인딩-타입-불일치--sampledtexture-vs-depthtexture)
- [3. CSM 그림자 샘플러 타입 — `sampler_comparison` 대신 수동 비교로 변경](#3-csm-그림자-샘플러-타입--sampler_comparison-대신-수동-비교로-변경)
- [4. SSAO: Compute Shader 방식에서 Render Pass 방식으로 전환](#4-ssao-compute-shader-방식에서-render-pass-방식으로-전환)
- [5. Skybox가 Deferred Lighting Pass에 의해 덮어씌워짐](#5-skybox가-deferred-lighting-pass에-의해-덮어씌워짐)
- [6. SSAO 샘플러 타입 불일치 — Filtering sampler incompatible with NonFiltering binding](#6-ssao-샘플러-타입-불일치--filtering-sampler-incompatible-with-nonfiltering-binding)

---

## 1. GBufferPass / DeferredLightingPass 빌드 오류 — `#ifndef __EMSCRIPTEN__` 가드

### 증상
Emscripten 빌드 시 `GBufferPass`, `DeferredLightingPass` 관련 심볼을 찾을 수 없는 링커 오류 또는 헤더에서 클래스 자체가 보이지 않는 컴파일 오류.

### 원인
두 클래스가 처음에 Vulkan 전용으로 구현되어 전체 클래스 선언이 `#ifndef __EMSCRIPTEN__` 가드 안에 있었음. Emscripten 빌드에서는 해당 타입 자체가 존재하지 않아 `Renderer.hpp`에서 멤버를 선언할 수 없었음.

추가로 `GBufferPass.hpp`는 Vulkan 전용 타입(`VkRenderPass`, `VkFramebuffer`, `VkDescriptorSetLayout`)을 헤더에 직접 노출하고 있었는데, 이를 Emscripten에서도 컴파일하려면 해당 타입들에 대한 stub 처리가 필요했음.

### 해결 방법
외부 `#ifndef __EMSCRIPTEN__` 가드를 제거하고, Vulkan 전용 타입은 헤더에서 플랫폼별 typedef로 대체:

```cpp
// GBufferPass.hpp
#ifndef __EMSCRIPTEN__
typedef struct VkRenderPass_T*          VkRenderPass;
typedef struct VkDescriptorSetLayout_T* VkDescriptorSetLayout;
// ... 나머지 Vulkan 타입
#else
using VkDescriptorSetLayout = void*;
using VkDescriptorSet       = void*;
#endif
```

클래스 내부 멤버는 `#ifndef __EMSCRIPTEN__` 가드로 조건부 선언 유지:

```cpp
#ifndef __EMSCRIPTEN__
    VkDescriptorSetLayout m_bindlessLayout = VK_NULL_HANDLE;
#ifdef __linux__
    VkRenderPass  m_nativeRenderPass  = VK_NULL_HANDLE;
    VkFramebuffer m_nativeFramebuffer = VK_NULL_HANDLE;
#endif
#endif
```

---

## 2. WebGPU 깊이 텍스처 바인딩 타입 불일치 — `SampledTexture` vs `DepthTexture`

### 증상
브라우저 콘솔에 WebGPU 검증 오류:

```text
[BindGroupLayout] binding 4: Expected to be DepthTexture, but got SampledTexture.
- While validating [BindGroupDescriptor "DeferredLightingBindGroup"]
```

### 원인
Vulkan에서는 깊이 텍스처를 `SampledTexture`로 바인딩하고 샘플러로 읽지만, WebGPU는 `texture_depth_2d`를 `SampledTexture`가 아닌 별도의 `DepthTexture` 바인딩 타입으로 구분함.

WGSL에서 `texture_depth_2d` 또는 `texture_depth_2d_array`로 선언된 바인딩은 레이아웃에서도 `DepthTexture`(혹은 `DepthTextureArray`)로 명시해야 함.

```wgsl
// deferred_lighting.wgsl
@group(0) @binding(4) var depthTex: texture_depth_2d;        // DepthTexture 필요
@group(0) @binding(6) var shadowCsmArray: texture_depth_2d_array;  // DepthTexture + View2DArray 필요
```

### 해결 방법
`DeferredLightingPass.cpp`의 WebGPU 경로에서 바인딩 타입을 `DepthTexture`로 변경:

```cpp
// DeferredLightingPass.cpp — WebGPU 전용 레이아웃
#ifdef __EMSCRIPTEN__
layoutDesc.entries = {
    entry(0,  S::Fragment, T::UniformBuffer),
    entry(1,  S::Fragment, T::SampledTexture),              // GBuffer0: texture_2d<f32>
    entry(2,  S::Fragment, T::SampledTexture),              // GBuffer1
    entry(3,  S::Fragment, T::SampledTexture),              // GBuffer2
    entry(4,  S::Fragment, T::DepthTexture),                // depthTex: texture_depth_2d
    entry(5,  S::Fragment, T::NonFilteringSampler),
    entry(6,  S::Fragment, T::DepthTexture, D::View2DArray),// shadowCsmArray: texture_depth_2d_array
    entry(7,  S::Fragment, T::NonFilteringSampler),
    // ...
};
```

Vulkan 경로에서는 `SampledTexture` 유지 (기존 동작 변경 없음).

> **원칙**: WGSL에서 `texture_depth_*` 타입을 사용하면 반드시 레이아웃에서도 `DepthTexture`로 선언해야 하며, 이는 Vulkan의 `sampled texture`와 다른 WebGPU 고유의 타입 시스템임.

---

## 3. CSM 그림자 샘플러 타입 — `sampler_comparison` 대신 수동 비교로 변경

### 배경
포팅 계획 단계에서는 CSM 그림자를 `textureSampleCompare` + `sampler_comparison`으로 구현할 예정이었음 (하드웨어 PCF).

```wgsl
// 초기 계획
let shadow = textureSampleCompare(shadowCsmArray, shadowSampler, uv, cascadeIndex, currentDepth - bias);
```

### 문제
`sampler_comparison`을 사용하면 Deferred Lighting 레이아웃에서 binding 7을 `ComparisonSampler`로 선언해야 하는데, 이 경우 동일 레이아웃에서 다른 non-depth 텍스처용 필터링 샘플러와 슬롯을 공유할 수 없음. 또한 `textureSampleCompare`는 WGSL에서 uniform control flow 제약이 있어 루프 안에서의 사용이 제한됨.

### 해결 방법
`textureSampleLevel`과 수동 비교로 PCF를 구현. `shadowSampler`를 `NonFilteringSampler`로 선언:

```wgsl
// deferred_lighting.wgsl — 실제 구현
@group(0) @binding(7) var shadowSampler: sampler;  // NonFiltering (comparison 아님)

// PCF 3×3 수동 비교
for (var x: i32 = -1; x <= 1; x++) {
    for (var y: i32 = -1; y <= 1; y++) {
        let offset = vec2<f32>(f32(x), f32(y)) * texelSize;
        let pcfDepth = textureSampleLevel(shadowCsmArray, shadowSampler,
                                          projCoords.xy + offset, cascadeIdx, 0);
        shadow += select(0.0, 1.0, currentDepth - bias > pcfDepth);
    }
}
```

레이아웃에서 binding 7은 `NonFilteringSampler`:

```cpp
entry(7, S::Fragment, T::NonFilteringSampler),  // shadowSampler (nearest, not comparison)
```

하드웨어 PCF가 아니므로 Vulkan 대비 그림자 경계가 약간 거칠 수 있으나, 동등한 시각적 품질을 유지함.

---

## 4. SSAO: Compute Shader 방식에서 Render Pass 방식으로 전환

### 배경
포팅 계획에서는 SSAO를 컴퓨트 셰이더로 구현할 예정이었음 (`ssao.comp.wgsl`):

```
// 초기 계획
Compute Shader:
  outputTex: texture_storage_2d<rgba8unorm, write>
  dispatch(width/8, height/8, 1)
```

### 문제
WebGPU에서 `r8unorm-storage` 포맷은 선택적 기능(optional feature)으로 브라우저별 지원이 불안정함. `rgba8unorm`으로 대체하면 메모리 사용이 4배 늘지만 허용 가능한 수준. 그러나 컴퓨트 방식은 비동기 컴퓨트 큐가 없는 WebGPU 단일 큐 모델에서 별도의 이점이 없고, 스토리지 텍스처 쓰기와 읽기 사이의 동기화 처리가 복잡해짐.

### 해결 방법
SSAO를 풀스크린 렌더 패스(Vertex + Fragment 셰이더)로 재설계:

```
// 실제 구현
shaders/ssao.wgsl:
  @vertex fn vs_main(...)   — 풀스크린 삼각형 (vertex_index 방식)
  @fragment fn fs_main(...) — 깊이에서 위치 재구성, 8-샘플 반구 AO 계산
  출력: R8Unorm 컬러 어태치먼트 (r = AO factor)
```

바인딩 구조도 단순화됨:

```wgsl
@group(0) @binding(0) var depthTex: texture_depth_2d;
@group(0) @binding(1) var samp:     sampler;           // NonFiltering
@group(0) @binding(2) var<uniform> params: SSAOParams;
```

컴퓨트 → 렌더 패스 전환으로 스토리지 텍스처 포맷 호환성 문제 및 동기화 복잡도가 해소됨.

---

## 5. Skybox가 Deferred Lighting Pass에 의해 덮어씌워짐

### 증상
Skybox가 렌더링되지 않음. HDR 렌더 타겟이 단색으로만 표시됨.

### 원인
렌더 패스 실행 순서:

```
Forward HDR Pass  (skybox → HDR 렌더 타겟에 기록)
  ↓
GBuffer Pass      (scene geometry → GBuffer MRT)
  ↓
Deferred Lighting (loadOp::Clear → HDR 렌더 타겟 초기화 → skybox 픽셀 소멸)
```

초기 구현에서 Deferred Lighting 패스가 HDR 렌더 타겟을 `loadOp::Clear`로 시작하여 이전에 그려진 Skybox를 지움.

### 해결 방법
Deferred Lighting WebGPU 경로에서 `loadOp::Load`로 변경:

```cpp
// Renderer.cpp — WebGPU 경로
dlColor.loadOp = rhi::LoadOp::Load;   // Clear 대신 Load — skybox 보존
```

Deferred Lighting 셰이더는 이미 `depth == 1.0`인 Sky 픽셀에 `discard`를 수행하므로 Skybox 픽셀에 쓰기가 발생하지 않음:

```wgsl
let depth = textureLoad(depthTex, fragCoord, 0);
if (depth >= 1.0) { discard; }
```

> Vulkan 렌더 그래프 경로는 처음부터 `loadOp = Load`였고 정상 동작. WebGPU 경로에서만 누락됐던 문제.

---

## 6. SSAO 샘플러 타입 불일치 — Filtering sampler incompatible with NonFiltering binding

### 오류 메시지

브라우저 콘솔에 매 프레임마다 WebGPU 검증 오류 반복 출력:

```text
Filtering sampler [Sampler (unlabeled)] is incompatible with non-filtering sampler binding.
- While validating entries[1] against { binding: 1, ..., sampler: {type: SamplerBindingType::NonFiltering} }.
- While validating [BindGroupDescriptor "WGSLSSAOBindGroup"] against [BindGroupLayout "WGSLSSAOLayout"]
```

BindGroup 생성 실패 → CommandBuffer 무효화 → `Queue.Submit` 실패가 매 프레임 연쇄 발생, SSAO 미동작.

### 발생 원인

`RHISamplerDesc::mipmapFilter`의 기본값이 `MipmapMode::Linear`이며, WebGPU는 `magFilter`, `minFilter`, `mipmapFilter` 세 필드가 모두 `Nearest`일 때만 해당 샘플러를 NonFiltering으로 분류함.

SSAO 샘플러 생성 코드에서 `mipmapFilter`를 명시하지 않아 기본값 `Linear`가 적용됨:

```cpp
// 문제 코드
rhi::SamplerDesc sd;
sd.magFilter = sd.minFilter = rhi::FilterMode::Nearest;
// mipmapFilter 미설정 → 기본값 MipmapMode::Linear → WebGPU는 Filtering으로 분류
```

SSAO 셰이더는 `texture_depth_2d`에 `textureSample` / `textureSampleLevel`을 사용하므로 WebGPU 스펙상 NonFiltering 샘플러가 필수임.

### 수정 내용

`mipmapFilter`를 명시적으로 `Nearest`로 설정:

```cpp
// 수정 코드 (Renderer.cpp — createSSAOPipelineWGSL)
rhi::SamplerDesc sd;
sd.magFilter    = sd.minFilter = rhi::FilterMode::Nearest;
sd.mipmapFilter = rhi::MipmapMode::Nearest;   // 추가 — NonFiltering 조건 충족
sd.addressModeU = sd.addressModeV = rhi::AddressMode::ClampToEdge;
```

> **주의**: 레이아웃을 `Sampler`(Filtering)로 변경하는 것은 해결책이 아님. `textureSampleLevel`을 `texture_depth_2d`에 사용하면 WebGPU 검증이 NonFiltering을 강제하므로, 레이아웃을 바꿔도 다른 검증 오류가 발생함.

---

## 최종 상태

| 항목 | 상태 |
| --- | --- |
| GBufferPass / DeferredLightingPass WebGPU 빌드 | ✅ |
| Depth 텍스처 바인딩 타입 (`DepthTexture`) | ✅ |
| CSM 그림자 PCF (수동 비교) | ✅ |
| SSAO 렌더 패스 방식 | ✅ |
| Skybox 보존 (`loadOp::Load`) | ✅ |
| SSAO NonFiltering 샘플러 | ✅ |
| 전체 셰이더 컴파일 (26개) | ✅ WGSL OK |

---

*작성일: 2026-05-14*
