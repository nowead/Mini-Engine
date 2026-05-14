# 최적화 & 하드웨어 추상화: 실제 트러블슈팅 경험

이 문서는 Mini-Engine 개발 과정에서 메모리 문제를 해결하거나, 처리 속도를 획기적으로 개선하거나, 하드웨어 간 차이를 성공적으로 추상화했던 구체적인 경험들을 정리합니다. 각 항목은 문제 상황, 근본 원인, 해결책, 그리고 배운 교훈을 포함합니다.

---

## 목차

1. [성능 최적화](#성능-최적화)
   - [GPU 인스턴싱: 드로우 콜 100배 감소](#gpu-인스턴싱-드로우-콜-100배-감소)
   - [WASM 바이너리 크기: "section too large" 링커 오류 해결](#wasm-바이너리-크기-section-too-large-링커-오류-해결)
2. [하드웨어 추상화](#하드웨어-추상화)
   - [Vulkan 1.1 vs 1.3: Linux에서 Dynamic Rendering 미지원](#vulkan-11-vs-13-linux에서-dynamic-rendering-미지원)
   - [플랫폼별 파이프라인 생성: RenderPass 요구사항](#플랫폼별-파이프라인-생성-renderpass-요구사항)
   - [이미지 레이아웃 전환: 수동 vs. 자동 배리어 처리](#이미지-레이아웃-전환-수동-vs-자동-배리어-처리)
   - [네이티브 핸들 타입 캐스팅: C++ 래퍼와 void* 연결](#네이티브-핸들-타입-캐스팅-c-래퍼와-void-연결)
   - [WebGPU 스토리지 텍스처 포맷 비호환성](#webgpu-스토리지-텍스처-포맷-비호환성)

---

## 성능 최적화

### GPU 인스턴싱: 드로우 콜 100배 감소

**출처**: `docs/refactoring/aaa-upgrade/GPU_INSTANCING.md`

#### 문제 상황

1,000개의 건물 오브젝트를 렌더링할 때 프레임마다 1,000번의 드로우 콜이 발생했다. 드로우 콜마다 CPU 오버헤드(상태 검증, 커맨드 기록, 드라이버 제출)가 누적되어 GPU가 CPU 병목에 묶이게 되었고, 결과적으로 ~10 FPS밖에 나오지 않았다.

```
인스턴싱 미적용:
  1,000개 오브젝트 × 드로우 콜 1회 = 드로우 콜 1,000회 → ~10 FPS
```

#### 근본 원인

각 오브젝트를 렌더 루프에서 개별적으로 제출했다. 동일한 지오메트리를 서로 다른 트랜스폼으로 배치할 때 GPU가 일괄 처리할 수 있는 메커니즘이 없었다.

#### 해결책

오브젝트별 데이터를 담는 두 번째 버텍스 버퍼를 추가해 GPU 인스턴싱을 구현했다.

```cpp
// 인스턴스 버퍼 레이아웃 — 입력 레이트를 Vertex에서 Instance로 변경
VertexBufferLayout instanceLayout{};
instanceLayout.stride    = sizeof(InstanceData);
instanceLayout.inputRate = VertexInputRate::Instance;   // 핵심 변경점
instanceLayout.attributes = { /* 모델 매트릭스 열, 색상 등 */ };

// 모든 인스턴스를 단 한 번의 드로우 콜로 처리
encoder->draw(vertexCount, instanceCount, 0, 0);
```

버텍스 셰이더는 버텍스별 어트리뷰트(`@location(0..n)`)와 인스턴스별 어트리뷰트(`@location(n+1..m)`)를 모두 받아 GPU에서 모델 매트릭스를 재구성한다.

#### 결과

| 지표 | 적용 전 | 적용 후 |
|------|---------|---------|
| 드로우 콜 수 (1,000개 오브젝트) | 1,000 | 1 |
| 프레임 레이트 | ~10 FPS | ~60 FPS |
| 성능 향상 | — | **실시간 ~6배, 드로우 콜 ~100배** |

#### 배운 점

- GPU 인스턴싱은 동일한 지오메트리가 반복되는 씬에서 가장 효과적인 단일 최적화다.
- CPU 비용은 프레임당 인스턴스 데이터 버퍼를 한 번 업데이트하는 것(`memcpy` 한 번)으로 줄어든다.
- `VertexInputRate::Instance`가 RHI 수준의 핵심 설정이며, 나머지는 셰이더 내부 처리다.

---

### WASM 바이너리 크기: "section too large" 링커 오류 해결

**출처**: `docs/refactoring/webgpu-backend/WASM_BUILD_TROUBLESHOOTING.md`

#### 문제 상황

Emscripten 링커가 WebAssembly 타겟 빌드 중 치명적인 오류를 내뱉으며 실패했다.

```
wasm-ld: error: section too large
```

`.wasm` 출력 파일조차 생성되지 않아 빌드 자체가 완전히 막혔다.

#### 근본 원인

`WebGPUCommon.hpp`에 `ToWGPUTextureFormat`, `ToWGPUBlendFactor` 등 25개 이상의 소규모 변환 함수가 `inline`으로 선언되어 있었다. 이 헤더를 13개 이상의 번역 단위(TU)가 포함했기 때문에 링커는 모든 함수 본문의 복사본을 13개씩 보게 되었다. WASM 섹션 크기 제한이 어떤 최적화 패스도 실행되기 전에 이미 초과되었다.

```
25개 함수 × 13개 TU ≈ .wasm 섹션에 325개의 중복 함수 본문
```

#### 해결책

1. **25개 변환 함수 전부를** `WebGPUCommon.hpp`에서 `WebGPUCommon.cpp`로 이동 — 정의는 하나, 선언은 13개.
2. **컴파일 플래그 변경**: `-O2` → `-Oz`(최대 크기 최적화), 컴파일과 링크 단계 모두에 `-flto` 추가.
3. **릴리즈 빌드**에서 `-g0`으로 디버그 정보 제거.

```cmake
target_compile_options(mini_engine_wasm PRIVATE -Oz -flto -g0)
target_link_options   (mini_engine_wasm PRIVATE -Oz -flto)
```

#### 결과

| 지표 | 적용 전 (추정) | 적용 후 |
|------|---------------|---------|
| `.wasm` 크기 | >500 KB (링크 실패) | **156 KB** |
| JS 글루 크기 | — | 154 KB |
| 빌드 상태 | **실패** | ✅ 성공 |

#### 배운 점

- 널리 포함되는 헤더의 `inline` 함수는 WASM 빌드에서 숨겨진 코드 크기 증폭기가 된다.
- 호출 지점이 많은 유틸리티는 헤더에 선언만 두고 단일 `.cpp`에 정의하는 방식을 선호해야 한다.
- `-Oz`와 `-flto`는 각각 단독으로 쓸 때보다 함께 쓸 때 WASM에서 훨씬 효과적이다.
- CI에서 `.wasm` 최종 크기를 항상 검증해야 한다 — 헤더를 많이 사용하면 조용히 크기가 불어난다.

---

## 하드웨어 추상화

### Vulkan 1.1 vs 1.3: Linux에서 Dynamic Rendering 미지원

**출처**: `docs/refactoring/layered-to-rhi/TROUBLESHOOTING.md` — Issue 3

#### 문제 상황

RHI의 `beginRenderPass()` 구현이 `vkCmdBeginRendering` / `vkCmdEndRendering`(Vulkan 1.3 / `VK_KHR_dynamic_rendering`)을 호출했다. Linux(lavapipe 소프트웨어 렌더러, Vulkan 1.1)에서는 매 프레임마다 검증 오류와 크래시가 발생했다.

```
Validation Error: VUID-vkCmdBeginRendering-dynamicRendering-06446
vkCmdBeginRendering() requires VK_KHR_dynamic_rendering or Vulkan 1.3.
```

#### 근본 원인

초기 RHI 설계가 Vulkan 1.3을 지원하는 macOS/Windows 하드웨어 드라이버를 기준으로 했다. Linux 개발/CI 머신은 Vulkan 1.1만 지원하며 dynamic rendering 확장을 구현하지 않은 lavapipe를 사용했다.

#### 해결책

`VulkanRHICommandEncoder` 내부에 플랫폼별 코드 경로를 추가했다.

```cpp
void VulkanRHICommandEncoder::beginRenderPass(const RenderPassDesc& desc) {
#ifdef __linux__
    // 전통적인 VkRenderPass — Vulkan 1.1 호환
    VkRenderPassBeginInfo info{};
    info.renderPass  = static_cast<VkRenderPass>(desc.nativeRenderPass);
    info.framebuffer = static_cast<VkFramebuffer>(desc.nativeFramebuffer);
    // ...
    vkCmdBeginRenderPass(m_cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
#else
    // Dynamic rendering — Vulkan 1.3 / macOS / Windows
    VkRenderingInfoKHR renderingInfo{};
    // ...
    vkCmdBeginRenderingKHR(m_cmd, &renderingInfo);
#endif
}
```

`RenderPassDesc`에 두 개의 불투명 필드를 추가했다.

```cpp
struct RenderPassDesc {
    // ... 기존 필드 ...
    void* nativeRenderPass   = nullptr;   // Linux에서 VkRenderPass
    void* nativeFramebuffer  = nullptr;   // Linux에서 VkFramebuffer
};
```

`VulkanRHISwapchain`이 `getRenderPass()`와 `getFramebuffer(imageIndex)`를 노출하여 호출 측이 Vulkan 타입에 직접 의존하지 않고 이 필드들을 채울 수 있게 했다.

#### 배운 점

하드웨어 추상화 레이어는 **동일한 API 내의 버전 단편화**도 반드시 고려해야 한다. Vulkan 1.1과 1.3은 렌더 패스 처리 방식이 근본적으로 다르다. 디스크립터 구조체의 nullable 불투명 필드를 통해 플랫폼 차이를 표현하면 RHI 인터페이스를 안정적으로 유지하면서 두 코드 경로를 모두 수용할 수 있다.

---

### 플랫폼별 파이프라인 생성: RenderPass 요구사항

**출처**: `docs/refactoring/layered-to-rhi/TROUBLESHOOTING.md` — Issue 4

#### 문제 상황

Linux에서 그래픽 파이프라인 생성 시 `VkGraphicsPipelineCreateInfo::renderPass`가 `VK_NULL_HANDLE`이어서 조용히 유효하지 않은 파이프라인이 만들어졌다. 이후 드로우 콜에서 크래시가 발생했다.

```
Validation Error: pipeline renderPass is VK_NULL_HANDLE
```

#### 근본 원인

Dynamic rendering이 비활성화된 경우(Vulkan 1.1) `VkGraphicsPipelineCreateInfo`는 유효한 `renderPass`를 요구한다. Vulkan 1.3에서는 이 필드가 무시된다. RHI가 항상 `VK_NULL_HANDLE`을 전달했는데, macOS/Windows에서는 동작했지만 Linux에서는 그렇지 않았다.

#### 해결책

`RenderPipelineDesc`에 `nativeRenderPass`를 추가하고, 엄격한 초기화 순서를 정의했다.

```cpp
struct RenderPipelineDesc {
    // ...
    void* nativeRenderPass = nullptr;  // Vulkan 1.1 (Linux)에서 필수
};
```

```cpp
// Linux에서의 올바른 순서:
bridge->createSwapchain(width, height, vsync);   // 1. VkRenderPass 내부 생성
auto rp = swapchain->getRenderPass();            // 2. 렌더 패스 가져오기
pipelineDesc.nativeRenderPass = reinterpret_cast<void*>(static_cast<VkRenderPass>(rp));
device->createRenderPipeline(pipelineDesc);      // 3. 이제 안전하게 생성 가능
```

**핵심 순서 규칙**: Linux에서는 어떤 파이프라인을 생성하기 전에 반드시 스왑체인(과 그 내부 `VkRenderPass`)이 먼저 존재해야 한다.

#### 배운 점

초기화 순서 버그는 크로스 플랫폼 코드에서 특히 파악하기 어렵다 — 한 플랫폼에서만 나타나기 때문이다. 요구되는 생성 순서를 명시적으로 문서화하고, 디버그 빌드에서 어서션으로 강제하는 것이 좋다.

---

### 이미지 레이아웃 전환: 수동 vs. 자동 배리어 처리

**출처**: `docs/refactoring/layered-to-rhi/TROUBLESHOOTING.md` — Issue 5

#### 문제 상황

이미지 레이아웃 전환을 위해 수동 `pipelineBarrier()` 호출을 추가한 후 Linux에서 검증 오류가 발생했다.

```
Validation Error: oldLayout must be VK_IMAGE_LAYOUT_UNDEFINED or
the current layout of the image. Expected PRESENT_SRC_KHR, got UNDEFINED.
```

#### 근본 원인

Linux에서 사용하는 전통적인 `VkRenderPass`는 서브패스 의존성 모델의 일부로 컬러 및 뎁스 어태치먼트의 이미지 레이아웃 전환을 자동으로 처리한다. 그런데 코드에서 `pipelineBarrier()`도 수동으로 호출하여 전환이 중복 실행되었고, 이미지가 예상치 못한 레이아웃 상태에 놓이게 되었다.

Vulkan 1.3의 dynamic rendering에서는 자동 전환이 없으므로 수동 배리어가 필수다.

#### 해결책

전통적인 렌더 패스를 사용하는 Linux에서는 수동 배리어를 건너뛴다.

```cpp
void VulkanRHICommandEncoder::transitionImageLayout(...) {
#ifdef __linux__
    // VkRenderPass가 레이아웃 전환을 소유함 — 중복 호출 금지.
    return;
#endif
    VkImageMemoryBarrier barrier{};
    // ... 배리어 설정 ...
    vkCmdPipelineBarrier(m_cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}
```

#### 배운 점

동일한 논리적 작업(이미지 레이아웃 전환)이 Vulkan 버전에 따라 완전히 다른 구현 전략을 요구한다. 추상화 레이어는 이 차이를 숨겨야 한다 — 호출 측은 *의도*(이 이미지를 셰이더 읽기 레이아웃으로 전환)만 선언하고, 백엔드가 올바른 메커니즘을 선택하는 구조가 이상적이다.

---

### 네이티브 핸들 타입 캐스팅: C++ 래퍼와 void* 연결

**출처**: `docs/game_logic/TROUBLESHOOTING.md`

#### 문제 상황

RHI는 디스크립터 구조체의 네이티브 Vulkan 핸들을 `void*`로 저장했다(예: `RenderPassDesc::nativeRenderPass`). `vk::RenderPass`(C++ 래퍼 타입)를 `void*`로 직접 캐스팅하면 컴파일 오류가 발생했다.

```
error: reinterpret_cast of type 'vk::RenderPass' to 'void*' is not allowed
```

#### 근본 원인

`vk::RenderPass`는 불투명한 64비트 정수 핸들을 감싸는 C++ 값 타입이다. 포인터가 아니므로 `reinterpret_cast<void*>`가 허용되지 않는다. 기반 C 타입인 `VkRenderPass`도 64비트 플랫폼에서는 `uint64_t`로 정의되어 마찬가지로 `void*`로 직접 캐스팅할 수 없다.

#### 해결책

이식성이 있고 명확하게 정의된 2단계 캐스트 패턴을 사용한다.

```cpp
// C++ 래퍼 → void*
void* toVoidPtr(vk::RenderPass rp) {
    return reinterpret_cast<void*>(static_cast<VkRenderPass>(rp));
}

// void* → C++ 래퍼
vk::RenderPass fromVoidPtr(void* ptr) {
    return static_cast<vk::RenderPass>(reinterpret_cast<VkRenderPass>(ptr));
}
```

`static_cast`로 C++ 래퍼와 그 기반 C 타입 간에 변환하고, `reinterpret_cast`로 정수 핸들과 포인터 간에 변환한다.

#### 배운 점

백엔드 고유 핸들을 범용 인터페이스(`void*`)에 저장해야 하는 하드웨어 추상화를 구현할 때는 핸들 타입마다 정형화된 캐스트 헬퍼를 만들어야 한다. 코드 전체에 흩어진 임기응변식 캐스트는 32비트 타겟에서 미묘한 포인터 크기 버그를 낳는다.

---

### WebGPU 스토리지 텍스처 포맷 비호환성

**출처**: `docs/refactoring/webgpu-backend/WEBGPU_RUNTIME_VALIDATION.md` — Issue 5

#### 문제 상황

BRDF LUT 컴퓨트 셰이더가 `RG16Float` 포맷의 스토리지 텍스처를 바인딩했다. WASM/WebGPU 빌드에서 바인드 그룹 생성 시 검증 오류가 발생했다.

```
[WebGPU] Validation error: Storage texture format 'rg16float' is not supported.
Supported formats: rgba8unorm, rgba8snorm, rgba8uint, rgba8sint,
                   rgba16float, r32float, r32uint, r32sint, ...
```

네이티브 Vulkan에서는 `RG16Float` 스토리지 이미지가 문제없이 동작했다.

#### 근본 원인

WebGPU에서 허용되는 스토리지 텍스처 포맷은 Vulkan의 엄격한 부분집합이다. `RG16Float`은 Vulkan에서 유효하지만 WebGPU의 허용 목록에 없다. 두 백엔드의 포맷 지원 테이블이 다르다.

#### 해결책

텍스처 생성 시점에 플랫폼별 포맷을 선택한다.

```cpp
TextureFormat brdfLutFormat =
#ifdef __EMSCRIPTEN__
    TextureFormat::RGBA16Float;   // WebGPU는 4채널 필요
#else
    TextureFormat::RG16Float;     // Vulkan — 더 촘촘한 패킹, 채널 낭비 없음
#endif
```

셰이더는 `RG`든 `RGBA` 텍스처든 관계없이 `.rg`만 읽도록 수정하여 두 경로가 기능적으로 동일하게 유지되도록 했다.

#### 배운 점

의미상 동일한 개념이라도 Vulkan과 WebGPU는 포맷 지원 테이블이 다르다. 크로스 백엔드 코드를 작성할 때는 각 텍스처 포맷을 두 API 스펙 모두에 검증하고, 불가피한 차이는 컴파일 타임 선택으로 처리해야 한다. 포맷 선택을 한 곳에 집중시키면 향후 백엔드 추가가 훨씬 수월해진다.

---

## 요약

| 경험 | 분류 | 임팩트 |
|------|------|--------|
| GPU 인스턴싱 | 성능 | 드로우 콜 1,000 → 1; ~10 FPS → ~60 FPS |
| WASM 바이너리 크기 | 메모리 / 빌드 | >500 KB (링크 실패) → 156 KB 동작 바이너리 |
| Dynamic Rendering (Vulkan 1.1 vs 1.3) | 하드웨어 추상화 | Linux 호환성; 크로스 플랫폼 렌더 패스 모델 |
| 파이프라인 RenderPass 요구사항 | 하드웨어 추상화 | Linux에서 조용히 생성되는 유효하지 않은 파이프라인 방지 |
| 이미지 레이아웃 전환 소유권 | 하드웨어 추상화 | 이중 전환 검증 오류 제거 |
| 네이티브 핸들 타입 캐스팅 | 하드웨어 추상화 | 백엔드 간 이식 가능한 `void*` 연동 패턴 |
| WebGPU 스토리지 텍스처 포맷 | 하드웨어 추상화 | 크로스 백엔드 BRDF LUT 컴퓨트 호환성 |
