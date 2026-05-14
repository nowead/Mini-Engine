# WebGPU 런타임 검증 오류 디버깅 기록

emdawnwebgpu 포팅 이후 WASM 빌드(`MiniEngine.html`)를 브라우저에서 실행할 때 발생한
WebGPU 런타임 검증 오류들의 원인 분석과 해결 과정을 기록한다.

---

## 배경

Vulkan 백엔드는 기본적으로 검증 레이어를 활성화하지 않으면 일부 레이아웃 불일치를 묵인한다.
반면 WebGPU(emdawnwebgpu)는 다음 시점에 엄격하게 검증을 수행한다:

- **파이프라인 생성 시**: 셰이더의 바인딩 접근 모드 vs 레이아웃 타입
- **바인드 그룹 생성 시**: 레이아웃 엔트리 수 일치 여부, 텍스처 뷰 aspect
- **텍스처 생성 시**: 포맷과 usage 조합의 지원 여부

검증 실패 시 `null` 대신 **invalid 객체(non-null, 사용 불가)** 를 반환하고,
이 객체를 커맨드 인코딩에서 사용하면 연쇄적인 `[Invalid CommandBuffer]` 오류가 발생한다.

---

## 오류 1: Invalid RenderPipeline "ShadowPipeline"

### 증상

```
[WebGPU Error] Validation: ...
[Invalid RenderPipeline "ShadowPipeline"] is invalid.
```

파이프라인 생성이 비정상적으로 완료되었지만 매 프레임 렌더링 시 invalid 오류가 발생.

### 원인

SSBO 바인드 그룹 레이아웃의 타입 불일치.

`building.wgsl`과 `shadow.wgsl` 모두 SSBO를 **읽기 전용**으로 선언하고 있었다:

```wgsl
// shadow.wgsl / building.wgsl
@group(1) @binding(0) var<storage, read> objectBuffer: ObjectBuffer;
```

그러나 `Renderer.cpp`의 SSBO 레이아웃은 **읽기-쓰기** 타입으로 지정되어 있었다:

```cpp
// 수정 전
ssboEntry.type = rhi::BindingType::StorageBuffer;  // → WGPUBufferBindingType_Storage (read-write)
```

WebGPU는 파이프라인 생성 시 셰이더의 `var<storage, read>`와 레이아웃의 `Storage`(read-write) 타입이
일치하지 않으면 파이프라인을 invalid로 만든다.

### 해결

`Renderer.cpp`의 SSBO 바인드 그룹 레이아웃 엔트리 타입을 변경:

```cpp
// 수정 후 (src/rendering/Renderer.cpp)
ssboEntry.type = rhi::BindingType::ReadOnlyStorageBuffer;
// → WebGPU: WGPUBufferBindingType_ReadOnlyStorage (var<storage, read>와 일치)
// → Vulkan: 동일하게 eStorageBuffer로 매핑되므로 호환성 유지
```

`visibleIndices` 엔트리도 동일하게 `ReadOnlyStorageBuffer`로 변경.

### 원칙

> WebGPU는 셰이더의 storage 접근 모드(`read` vs `read_write`)와 레이아웃의 바인딩 타입이
> **정확히** 일치해야 파이프라인을 생성할 수 있다.
> Vulkan은 두 타입 모두 `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`로 처리하지만,
> WebGPU는 `ReadOnlyStorage`와 `Storage`를 별개의 타입으로 취급한다.

---

## 오류 2: Invalid BindGroup "Building Bind Group with Shadow + IBL"

### 증상

```
[WebGPU Error] Validation: [Invalid BindGroup "Building Bind Group with Shadow + IBL"] is invalid.
While encoding [RenderPassEncoder].SetBindGroup(0, [Invalid BindGroup ...])
```

### 원인

섀도우 맵 텍스처 뷰의 aspect 불일치.

`building.wgsl`은 섀도우 맵을 `texture_depth_2d`(= `WGPUTextureSampleType_Depth`)로 바인딩한다:

```wgsl
@group(0) @binding(1) var shadowMapTex: texture_depth_2d;
```

이 바인딩 타입을 충족하려면 텍스처 뷰를 `WGPUTextureAspect_DepthOnly`로 생성해야 한다.
그러나 `WebGPURHITexture.cpp`는 모든 뷰를 `WGPUTextureAspect_All`로 생성하고 있었다:

```cpp
// 수정 전 (src/rhi/backends/webgpu/src/WebGPURHITexture.cpp)
viewDesc.aspect = WGPUTextureAspect_All;  // 뎁스 포맷에는 허용되지 않음
```

### 해결

뎁스 포맷을 자동 감지하여 `DepthOnly` aspect를 적용:

```cpp
// 수정 후 (src/rhi/backends/webgpu/src/WebGPURHITexture.cpp)
auto fmt = desc.format;
bool isDepthFormat = (fmt == rhi::TextureFormat::Depth32Float ||
                      fmt == rhi::TextureFormat::Depth24Plus ||
                      fmt == rhi::TextureFormat::Depth24PlusStencil8 ||
                      fmt == rhi::TextureFormat::Depth16Unorm);
viewDesc.aspect = isDepthFormat ? WGPUTextureAspect_DepthOnly : WGPUTextureAspect_All;
```

`DepthOnly`는 뎁스 어태치먼트와 셰이더 샘플링 모두에서 유효하다.

### 원칙

> WebGPU에서 `texture_depth_2d`로 바인딩되는 텍스처 뷰는 반드시 `DepthOnly` aspect로 생성해야 한다.
> `All` aspect는 스텐실 결합 포맷에만 사용하며, 단순 뎁스 포맷(`Depth32Float` 등)에는 허용되지 않는다.

---

## 오류 3: Visible Indices 버퍼 미초기화로 인한 렌더링 버그

### 증상

화면에 건물들이 모두 한 위치(object 0)에 렌더링되는 시각적 버그. (에러 메시지 없음)

### 원인

`building.wgsl`은 GPU 프러스텀 컬링 결과를 담은 visible indices 버퍼를 통해 실제 오브젝트 인덱스를 조회한다:

```wgsl
let actualIndex = visibleIndices.indices[input.instanceIndex];
let obj = objectBuffer.objects[actualIndex];
```

WASM에서는 프러스텀 컬링을 비활성화(`#ifdef __EMSCRIPTEN__` 조기 반환)했기 때문에
`visibleIndices` 버퍼가 GPU 초기화값(전체 0)으로 남아 있었다.
결과적으로 `actualIndex`가 항상 0이 되어 모든 건물이 object 0의 worldMatrix를 사용.

### 해결

두 가지 수정이 필요했다:

**① `CopyDst` usage 추가** (CPU에서 버퍼 쓰기 허용):

```cpp
// src/rendering/Renderer.cpp
visDesc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::CopyDst;
```

**② WASM early-return 전에 identity 매핑 기록**:

```cpp
// performFrustumCulling() 내 WASM 경로
#ifdef __EMSCRIPTEN__
    if (visibleIndicesBuffers[frameIndex] && objectCount > 0) {
        std::vector<uint32_t> identityIndices(objectCount);
        for (uint32_t i = 0; i < objectCount; ++i) identityIndices[i] = i;
        visibleIndicesBuffers[frameIndex]->write(identityIndices.data(), objectCount * sizeof(uint32_t));
    }
    return;
#endif
```

### 원칙

> GPU 컬링을 비활성화하는 경우, 셰이더가 해당 버퍼를 읽는다면 CPU에서 identity 매핑을 직접 써줘야 한다.
> GPU 초기화(영벡터)에 의존하면 안 된다.

---

## 오류 4: RHI Main Bind Group 엔트리 수 불일치

### 증상

```
[WebGPU Error] Validation: Number of entries (1) did not match the expected number of entries (2)
for [BindGroupLayoutInternal "RHI Main Bind Group Layout"].
Expected layout: [
  { binding: 0, visibility: Vertex, buffer: Uniform },
  { binding: 1, visibility: Fragment, texture: Float }
]
```

### 원인

레거시 `createRHIBindGroups()`에서 레이아웃은 2개 엔트리(UBO + SampledTexture)로 선언했지만,
실제 바인드 그룹 생성 시 UBO만 1개 추가하고 있었다:

```cpp
// Renderer.cpp - createRHIBindGroups()
// 레이아웃: binding 0 (Uniform) + binding 1 (SampledTexture) — 2개 선언
// 바인드 그룹: binding 0 (UBO)만 추가 — 1개만 제공
bindGroupDesc.entries.push_back(
    rhi::BindGroupEntry::Buffer(0, rhiUniformBuffers[i].get())
);
// binding 1 없음 → WebGPU 검증 실패
```

이 `rhiPipeline`/`rhiBindGroups` 경로는 구형 `slang.spv` 셰이더를 사용하는 레거시 렌더 패스로,
WASM에서는 `slang.spv`가 존재하지 않아 `rhiPipeline`이 항상 null이다.
따라서 바인드 그룹이 실제로 사용되지는 않지만, **생성 시점에 검증 오류가 발생**한다.

Vulkan은 레이아웃에 선언된 엔트리가 바인드 그룹에 없어도 묵인하는 경우가 있어 이 버그가 드러나지 않았다.

### 해결

실제로 바인드 그룹에 제공하지 않는 binding 1(SampledTexture)을 레이아웃에서 제거:

```cpp
// src/rendering/Renderer.cpp - 수정 후
// binding 1 선언 삭제: 레이아웃과 바인드 그룹이 모두 1개 엔트리로 일치
layoutDesc.entries.push_back(uboEntry);  // binding 0만 유지
```

### 원칙

> WebGPU는 바인드 그룹 생성 시 레이아웃에 선언된 엔트리 수와 실제 제공된 엔트리 수가
> **정확히 일치해야** 한다. 레이아웃에 선언된 엔트리는 반드시 바인드 그룹에서도 제공해야 한다.

---

## 오류 5: IBL BRDF LUT 텍스처 포맷 + Usage 불일치

### 증상

```
[WebGPU Error] Validation: The texture usage (TextureUsage::(TextureBinding|StorageBinding))
includes TextureUsage::StorageBinding, which is incompatible with the format (TextureFormat::RG16Float).
While validating [TextureDescriptor "IBL_BRDF_LUT"].
```

이 오류의 연쇄로 `[Invalid Texture "IBL_BRDF_LUT"]`, `[Invalid TextureView]`,
`[Invalid BindGroup "BRDF_LUT_BindGroup"]`, `[Invalid CommandBuffer]`까지 발생.

### 원인

`IBLManager::createTextures()`에서 BRDF LUT 텍스처를 항상 `RG16Float` 포맷으로 생성했다:

```cpp
// 수정 전 (src/rendering/IBLManager.cpp)
desc.format = rhi::TextureFormat::RG16Float;
desc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
```

WebGPU는 `RG16Float` 포맷에 `StorageBinding` usage를 허용하지 않는다.
(`rgba16float`만 storage texture 접근 가능, `rg16float` 미지원)

`generateBRDFLut()`의 바인드 그룹 레이아웃에는 이미 `#ifdef __EMSCRIPTEN__` 분기로
`RGBA16Float`을 지정하고 있었지만, **텍스처 생성 자체에는 분기가 없었다**.

### 해결

텍스처 생성에도 동일한 `#ifdef` 분기 추가:

```cpp
// 수정 후 (src/rendering/IBLManager.cpp)
#ifdef __EMSCRIPTEN__
    constexpr rhi::TextureFormat brdfTexFormat = rhi::TextureFormat::RGBA16Float;
#else
    constexpr rhi::TextureFormat brdfTexFormat = rhi::TextureFormat::RG16Float;
#endif
// ...
desc.format = brdfTexFormat;  // 생성 포맷도 WASM에서 RGBA16Float 사용
```

### 원칙

> WebGPU에서 storage texture로 사용 가능한 포맷은 제한적이다.
> `rgba16float`은 지원하지만 `rg16float`은 지원하지 않는다.
> 셰이더의 `@binding storageTexture format`과 텍스처 생성 포맷을 **항상 일치**시켜야 한다.

---

## 오류 6: Frustum Cull 레이아웃의 읽기 전용 SSBO 타입 불일치

### 증상

```
[WebGPU Error] Validation: The buffer type in the shader (BufferBindingType::ReadOnlyStorage)
is not compatible with the type in the layout (BufferBindingType::Storage).
```

### 원인

`frustum_cull.comp.wgsl`의 binding 1(objectBuffer)은 읽기 전용으로 선언되어 있었다:

```wgsl
@group(0) @binding(1) var<storage, read> objectBuffer: ObjectBuffer;  // 읽기 전용
@group(0) @binding(2) var<storage, read_write> indirect: IndirectDrawCommand;  // 읽기-쓰기
@group(0) @binding(3) var<storage, read_write> visibleIndices: VisibleIndicesBuffer;  // 읽기-쓰기
```

그러나 `createCullingPipeline()`의 레이아웃에서 binding 1도 `StorageBuffer`(read-write)로 지정했다:

```cpp
// 수정 전
objEntry.type = rhi::BindingType::StorageBuffer;  // read-write → 셰이더의 read와 불일치
```

### 해결

binding 1만 `ReadOnlyStorageBuffer`로 변경 (binding 2, 3은 `read_write`이므로 `StorageBuffer` 유지):

```cpp
// 수정 후 (src/rendering/Renderer.cpp)
objEntry.type = rhi::BindingType::ReadOnlyStorageBuffer;  // var<storage, read>와 일치
```

### 원칙

> 컴퓨트 셰이더의 각 바인딩에 대해 `read`/`read_write` 접근 모드를 확인하고
> 레이아웃 타입을 정확히 일치시켜야 한다.
> `atomicAdd`가 필요한 버퍼(`IndirectDrawCommand`, `VisibleIndicesBuffer`)는 반드시 `read_write`.

---

## 수정된 파일 목록

| 파일 | 오류 | 수정 내용 |
|------|------|-----------|
| `src/rendering/Renderer.cpp` | 오류 1 | SSBO 레이아웃 → `ReadOnlyStorageBuffer` |
| `src/rhi/backends/webgpu/src/WebGPURHITexture.cpp` | 오류 2 | 뎁스 뷰 aspect → `DepthOnly` |
| `src/rendering/Renderer.cpp` | 오류 3 | WASM early-return 전 identity 인덱스 기록 |
| `src/rendering/Renderer.cpp` | 오류 4 | 레거시 레이아웃에서 미사용 SampledTexture 제거 |
| `src/rendering/IBLManager.cpp` | 오류 5 | BRDF LUT 텍스처 포맷 WASM에서 `RGBA16Float` |
| `src/rendering/Renderer.cpp` | 오류 6 | Cull 레이아웃 binding 1 → `ReadOnlyStorageBuffer` |

---

## WebGPU 검증 관련 주요 규칙 요약

### 버퍼 바인딩 타입

| WGSL 선언 | RHI 타입 | WebGPU API |
|-----------|----------|------------|
| `var<uniform>` | `UniformBuffer` | `BufferBindingType::Uniform` |
| `var<storage, read>` | `ReadOnlyStorageBuffer` | `BufferBindingType::ReadOnlyStorage` |
| `var<storage, read_write>` | `StorageBuffer` | `BufferBindingType::Storage` |

### 텍스처 바인딩 타입

| WGSL 선언 | 요구 조건 |
|-----------|-----------|
| `texture_2d<f32>` | `SampledTexture`, aspect: `All` |
| `texture_depth_2d` | `DepthTexture`, aspect: **`DepthOnly`** |
| `texture_storage_2d<rgba16float, write>` | `StorageTexture`, format: `RGBA16Float` |

### Storage Texture 포맷 지원 (WebGPU)

WebGPU 기본 기능(feature 없이)에서 storage texture로 사용 가능한 포맷:

- `rgba8unorm`, `rgba8snorm`, `rgba8uint`, `rgba8sint`
- `rgba16uint`, `rgba16sint`, `rgba16float`
- `r32uint`, `r32sint`, `r32float`
- `rg32uint`, `rg32sint`, `rg32float`
- `rgba32uint`, `rgba32sint`, `rgba32float`

**`rg16float`, `rg8*` 계열은 storage texture로 사용 불가.**
