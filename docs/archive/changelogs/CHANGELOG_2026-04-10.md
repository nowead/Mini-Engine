# 변경 이력 — 2026-04-10

> 작업 범위: Windows Native Build 지원 + Vulkan Dynamic Rendering 밸리데이션 레이어 에러 전량 수정

---

## 1. 개요

Phase 1~4까지의 모든 기능을 **Windows 네이티브 환경(RTX 4070)** 에서 Vulkan 밸리데이션 에러 없이 실행 가능하도록 완성.

주요 작업 두 축:

1. **Windows 빌드 인프라** — CMakePresets, Makefile, RendererBridge 플랫폼 분기
2. **Dynamic Rendering 이미지 레이아웃 동기화** — `s_imageLayouts` 글로벌 트래커 + 첫 프레임 초기화 배리어

> Windows Vulkan Dynamic Rendering(1.3)은 Linux의 traditional render pass와 달리 `initialLayout` / `finalLayout` 자동 전환이 없다. 모든 레이아웃 전환을 명시적 `vkCmdPipelineBarrier`로 처리해야 한다.

---

## 2. Windows 빌드 인프라

### 2.1 CMakePresets.json — Windows 프리셋 추가

```json
{
  "name": "windows-release",
  "generator": "Ninja",
  "binaryDir": "${sourceDir}/build-windows",
  "cacheVariables": {
    "CMAKE_BUILD_TYPE": "Release",
    "CMAKE_TOOLCHAIN_FILE": "C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake",
    "VCPKG_TARGET_TRIPLET": "x64-windows"
  }
}
```

빌드 방법:
```
cmake --preset windows-release
cmake --build build-windows --target MiniEngine -j8
```

### 2.2 Makefile — Windows 타겟 추가

| 타겟 | 명령 |
|---|---|
| `make windows` | `cmake --build build-windows` |
| `make windows-shaders` | `cmake --build build-windows --target building_shaders` |
| `make run-windows` | `build-windows/MiniEngine.exe` |

### 2.3 CMakeLists.txt — 누락 셰이더 추가

`building_shaders` 타겟에 다음 셰이더 컴파일 규칙 추가 (기존 누락):

| 소스 | 출력 |
|---|---|
| `skybox.vert.glsl` | `skybox.vert.spv` |
| `skybox.frag.glsl` | `skybox.frag.spv` |
| `particle.vert.glsl` | `particle.vert.spv` |
| `particle.frag.glsl` | `particle.frag.spv` |

### 2.4 RendererBridge 확장

**파일:** `src/rendering/RendererBridge.hpp/.cpp`

Windows에서 Renderer를 통합 래핑하는 인터페이스 보강:
- `getDevice()`, `getGraphicsQueue()` 게터 추가
- `getCurrentVkImage()` swapchain 이미지 핸들 접근자
- 플랫폼별 `nativeRenderPass` 전달 경로 정리

---

## 3. Vulkan 이미지 레이아웃 동기화 시스템

### 3.1 `s_imageLayouts` — 글로벌 레이아웃 트래커

**파일:** `src/rhi/backends/vulkan/src/VulkanRHICommandEncoder.cpp`

```cpp
static std::unordered_map<VkImage, vk::ImageLayout> s_imageLayouts;
```

- 모든 `VulkanRHICommandEncoder` / `VulkanRHIRenderPassEncoder` 인스턴스가 공유하는 정적 맵
- `transitionTextureLayout()` 호출 시 자동 갱신
- `notifyImageLayoutChange()` 정적 메서드로 외부 코드(Render Graph 등)에서도 갱신 가능

**이유:** Dynamic Rendering에서는 render pass가 레이아웃을 추적하지 않으므로, 각 렌더 패스가 시작될 때 이전 레이아웃이 무엇이든 올바른 배리어를 자동으로 선택해야 한다.

### 3.2 `emitBarrierToColor` / `emitBarrierToDepth` 람다

**파일:** `VulkanRHIRenderPassEncoder` 생성자 (동적 렌더링 분기)

렌더 패스 시작 전, 각 컬러/뎁스 어태치먼트에 대해:
1. `s_imageLayouts`에서 현재 레이아웃 조회 (없으면 UNDEFINED)
2. 이미 목표 레이아웃이면 배리어 생략
3. 아니면 올바른 소스 스테이지로 배리어 발행

**3-버킷 배리어 전략 (srcStageMask 유효성 보장):**

| 버킷 | 소스 레이아웃 | `srcStageMask` |
|---|---|---|
| `fromUndefinedBarriers` | UNDEFINED | `TOP_OF_PIPE` (srcAccess = 0) |
| `fromShaderReadBarriers` | SHADER_READ_ONLY | `FRAGMENT_SHADER \| COMPUTE_SHADER` |
| `fromColorAttachBarriers` | COLOR_ATTACHMENT / 기타 | `COLOR_ATTACHMENT_OUTPUT \| LATE_FRAGMENT_TESTS` |

> **이유:** Vulkan 스펙상 `srcStageMask = TOP_OF_PIPE` 일 때 `srcAccessMask` 는 0이어야 한다. SHADER_READ 접근 플래그와 TOP_OF_PIPE 스테이지를 함께 쓰면 밸리데이션 에러가 발생한다.

### 3.3 `VulkanRHITextureView::m_parentImage`

**파일:** `src/rhi/backends/vulkan/include/rhi/vulkan/VulkanRHITexture.hpp`  
**파일:** `src/rhi/backends/vulkan/src/VulkanRHITexture.cpp`

```cpp
VkImage m_parentImage = VK_NULL_HANDLE;
VkImage getParentImage() const { return m_parentImage; }
```

렌더 패스 어태치먼트는 `RHITextureView*` 로 전달되므로, 배리어 발행에 필요한 `VkImage` 핸들을 뷰에서 역추적할 수 있도록 부모 이미지를 저장.

### 3.4 `notifyImageLayoutChange` 정적 메서드

**파일:** `VulkanRHICommandEncoder.hpp/.cpp`

```cpp
static void notifyImageLayoutChange(VkImage image, vk::ImageLayout newLayout);
```

Render Graph처럼 인코더 외부에서 직접 `vkCmdPipelineBarrier`를 발행하는 코드가 `s_imageLayouts`를 동기화할 수 있도록 제공.

**파일:** `src/rendering/graph/RenderGraph.cpp`

```cpp
// transitionTex 람다 내부에서 배리어 발행 후 호출
VulkanRHICommandEncoder::notifyImageLayoutChange(
    static_cast<VkImage>(img), needed.layout);
```

---

## 4. 첫 프레임 초기화 배리어

### 4.1 문제: Windows 밸리데이션 레이어의 one-time-submit 추적 한계

Windows Vulkan 밸리데이션 레이어는 **별도 커맨드 버퍼 제출**에서 발생한 레이아웃 전환을 메인 프레임 커맨드 버퍼의 드로우콜 검증 시 반영하지 않는다.

IBL 큐브맵, BRDF LUT, SkyboxRenderer 더미 큐브맵은 모두 초기화 단계의 one-time-submit으로 전환되므로, 메인 프레임에서 밸리데이션 레이어가 이를 UNDEFINED로 오인한다.

### 4.2 `IBLManager::emitInitializationBarriers()`

**파일:** `src/rendering/IBLManager.hpp/.cpp`

```cpp
void IBLManager::emitInitializationBarriers(rhi::RHICommandEncoder* encoder) {
    // UNDEFINED → ShaderReadOnly (oldLayout=Undefined: 스펙상 항상 유효)
    encoder->transitionTextureLayout(m_envCubemap.get(),    Undefined, ShaderReadOnly);
    encoder->transitionTextureLayout(m_irradianceMap.get(), Undefined, ShaderReadOnly);
    encoder->transitionTextureLayout(m_prefilteredMap.get(),Undefined, ShaderReadOnly);
    encoder->transitionTextureLayout(m_brdfLut.get(),       Undefined, ShaderReadOnly);
}
```

### 4.3 SkyboxRenderer 더미 큐브맵

환경 맵이 로드되지 않은 경우 SkyboxRenderer는 1×1×6 더미 큐브맵을 사용한다. 이 텍스처도 첫 프레임에 UNDEFINED → SHADER_READ_ONLY 배리어가 필요.

**파일:** `src/rendering/SkyboxRenderer.hpp`

```cpp
rhi::RHITexture* getDummyEnvTexture() const { return m_dummyEnvTexture.get(); }
```

### 4.4 `Renderer::drawFrame()` 첫 프레임 블록

**파일:** `src/rendering/Renderer.hpp` — `bool m_iblBarriersEmitted = false;`  
**파일:** `src/rendering/Renderer.cpp`

```cpp
#if !defined(__linux__) && !defined(__EMSCRIPTEN__)
if (!m_iblBarriersEmitted) {
    if (iblManager && iblManager->isInitialized())
        iblManager->emitInitializationBarriers(encoder.get());

    // 더미 큐브맵 (HDR 미로드 시 사용)
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
```

이 블록은 메인 프레임 커맨드 버퍼의 **최초 커맨드 위치** (GPU Profiler beginFrame 이후, 첫 렌더 패스 이전)에 배치되어 밸리데이션 레이어가 동일 커맨드 버퍼 내에서 배리어를 확인할 수 있도록 한다.

---

## 5. `barrierUndef` 블록 제거

**파일:** `src/rendering/Renderer.cpp`

기존에 G-Buffer용으로 `#if !defined(__linux__)` 블록 내에 존재하던 수동 배리어:

```cpp
// 제거된 코드
auto barrierUndef = [&](rhi::RHITexture* tex) {
    ve->getCommandBuffer().pipelineBarrier(...
        oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, ...);
};
barrierUndef(gBufferPass->getGBuffer0());
barrierUndef(gBufferPass->getGBuffer1());
barrierUndef(gBufferPass->getGBuffer2());
```

**제거 이유:** `s_imageLayouts` 트래커를 갱신하지 않아 밸리데이션 레이어 추적 상태와 불일치 발생. 현재는 `VulkanRHIRenderPassEncoder`의 `emitBarrierToColor`가 동일한 역할을 정확한 소스 레이아웃으로 처리한다.

---

## 6. 수정된 Vulkan 밸리데이션 에러 목록

| 에러 | 이미지 | 원인 | 수정 |
|---|---|---|---|
| `UNDEFINED` expected `COLOR_ATTACHMENT` (첫 프레임) | HDR, Depth, GBuffer | Dynamic Rendering 자동 전환 없음 | `emitBarrierToColor/Depth` 람다 |
| `COLOR_ATTACHMENT` expected `SHADER_READ_ONLY` (2+ 프레임) | HDR, Depth, GBuffer | `s_imageLayouts` 없어서 Render Graph 전환 후 상태 불인지 | `s_imageLayouts` + `notifyImageLayoutChange` |
| `srcAccessMask` / `srcStageMask` 불일치 | 전체 | 단일 배리어 콜로 SHADER_READ + TOP_OF_PIPE 혼용 | 3-버킷 배리어 분리 |
| IBL 큐브맵 `UNDEFINED` (6 array layers) | envCubemap 등 | One-time-submit 전환 VL 미추적 | `emitInitializationBarriers` |
| 더미 큐브맵 `UNDEFINED` | `0x500000000050` | SkyboxRenderer 더미 텍스처 전환 누락 | getDummyEnvTexture + 배리어 |
| GBuffer `SHADER_READ_ONLY` expected `COLOR_ATTACHMENT` (2+ 프레임) | GBuffer0/1/2 | `barrierUndef`가 잘못된 oldLayout으로 배리어 발행 | `barrierUndef` 제거 |

**최종 상태:** RTX 4070 / Windows 11, Vulkan 밸리데이션 레이어 에러 **0건**.

---

## 7. 빌드 변경 요약

| 파일 | 변경 내용 |
|---|---|
| `CMakePresets.json` | `windows-release` 프리셋 추가 |
| `CMakeLists.txt` | skybox/particle 셰이더 컴파일 타겟 추가 |
| `Makefile` | `windows`, `windows-shaders`, `run-windows` 타겟 추가 |
| `VulkanRHICommandEncoder.hpp/.cpp` | `s_imageLayouts`, `notifyImageLayoutChange`, 3-버킷 배리어 |
| `VulkanRHITexture.hpp/.cpp` | `VulkanRHITextureView::m_parentImage` |
| `IBLManager.hpp/.cpp` | `emitInitializationBarriers()` |
| `SkyboxRenderer.hpp` | `getDummyEnvTexture()` |
| `Renderer.hpp/.cpp` | `m_iblBarriersEmitted`, 첫 프레임 배리어 블록, `barrierUndef` 제거 |
| `RenderGraph.cpp` | `notifyImageLayoutChange` 호출 추가 |

---

## 8. 로드맵 진행 현황

| Phase | 내용 | 상태 |
|---|---|---|
| Phase 1 | Vulkan Post-Processing (HDR, Bloom, SSAO, FXAA) | ✅ 완료 |
| Phase 2 | Render Graph (자동 sync2 배리어 추론) | ✅ 완료 |
| Phase 3 | Deferred Rendering + 4-Cascade CSM | ✅ 완료 |
| Phase 4 | VMA Custom Pools + Bindless Textures | ✅ 완료 |
| **Windows** | **Windows Native Build + 밸리데이션 전량 수정** | ✅ **완료** |
| Phase 5 | 센서 시뮬레이션 (LiDAR / 카메라) | 미착수 |
