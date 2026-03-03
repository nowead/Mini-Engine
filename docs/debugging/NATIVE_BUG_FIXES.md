# 네이티브(Vulkan) 빌드/런타임 버그 수정 기록

WASM 포팅 작업 과정에서 발견된 네이티브 빌드·런타임 버그들의 원인 분석과 해결 과정을 기록한다.
이전 WASM 빌드 에러로 인해 네이티브 빌드가 실패하는 동안 숨겨져 있다가 드러난 버그들이다.

---

## 버그 1: ImGuiManager 헤더/구현 불일치 (빌드 에러)

### 증상

```
/Users/.../src/ui/ImGuiManager.cpp:63:40: error: use of undeclared identifier 'm_targetBuildingCount'
/Users/.../src/ui/ImGuiManager.cpp:205:48: error: use of undeclared identifier 'm_gpuTiming'
```

16개의 컴파일 에러로 `MiniEngine` 빌드 타겟 전체가 실패.

### 원인

`ImGuiManager.cpp`에 Phase 4.1 기능(스트레스 테스트 슬라이더, GPU 타이밍 패널)이 추가됐으나,
대응하는 멤버 선언이 `ImGuiManager.hpp`에 없었다.

```cpp
// ImGuiManager.cpp에서 사용하지만 헤더에 없던 멤버들
m_targetBuildingCount  // int  — 빌딩 카운트 슬라이더 값
m_buildingCountChanged // bool — 카운트 변경 플래그
m_gpuTiming            // struct{ cullingMs, shadowMs, mainPassMs }
```

### 해결

`ImGuiManager.hpp`에 누락된 선언 추가:

```cpp
// Phase 4.1: Stress test controls
int  m_targetBuildingCount  = 1000;
bool m_buildingCountChanged = false;

// Phase 4.1: GPU timing
struct GPUTiming {
    float cullingMs  = 0.0f;
    float shadowMs   = 0.0f;
    float mainPassMs = 0.0f;
};
GPUTiming m_gpuTiming;
```

Application ↔ UI 간 통신을 위한 공개 API도 함께 추가:

```cpp
// UI → Application: 변경된 빌딩 카운트 읽기 (플래그 자동 초기화)
bool getBuildingCountChange(int& outCount);

// Application → UI: GPU 타이밍 데이터 갱신
void setGPUTiming(const GPUTiming& timing);
```

### 원칙

> `.cpp`에 새 멤버/기능을 추가할 때는 반드시 헤더에도 선언을 함께 추가해야 한다.
> Vulkan(관대) vs WebGPU(엄격)와 달리, C++ 컴파일러는 이 문제를 항상 잡아준다.

---

## 버그 2: VulkanRHITexture::createView() — Undefined 포맷 처리 누락 (크래시)

### 증상

```
[SkyboxRenderer] Shaders created successfully
Unsupported texture format
make: *** [run] Error 1
```

앱 시작 직후 `std::runtime_error("Unsupported texture format")` 발생.

### 원인

`VulkanRHITexture::createView()`가 `desc.format == TextureFormat::Undefined`일 때
그대로 `ToVkFormat(Undefined)`를 호출하여 throw.

```cpp
// 수정 전 (VulkanRHITexture.cpp)
std::unique_ptr<RHITextureView> VulkanRHITexture::createView(const TextureViewDesc& desc) {
    return std::make_unique<VulkanRHITextureView>(m_device, m_image, desc);
    // ↑ desc.format = Undefined이면 ToVkFormat이 default case로 throw
}
```

트리거: `SkyboxRenderer::createBindGroups()`에서 dummy cubemap 뷰 생성 시
`viewDesc.format`을 명시하지 않아 기본값 `Undefined`로 전달됨.

```cpp
// SkyboxRenderer.cpp — format 미설정
rhi::TextureViewDesc viewDesc;
viewDesc.dimension = rhi::TextureViewDimension::ViewCube;
viewDesc.arrayLayerCount = 6;
// viewDesc.format = Undefined (기본값) → crash
m_dummyEnvView = m_dummyEnvTexture->createView(viewDesc);
```

WebGPU 백엔드(`WebGPURHITexture::createView()`)에는 이미 Undefined fallback이 있었지만
Vulkan 백엔드에는 없었다.

### 해결

Vulkan 백엔드에 WebGPU와 동일한 fallback 추가:

```cpp
// 수정 후 (VulkanRHITexture.cpp)
std::unique_ptr<RHITextureView> VulkanRHITexture::createView(const TextureViewDesc& desc) {
    TextureViewDesc actualDesc = desc;
    if (actualDesc.format == TextureFormat::Undefined) {
        actualDesc.format = m_format;  // 텍스처 자체의 포맷으로 fallback
    }
    return std::make_unique<VulkanRHITextureView>(m_device, m_image, actualDesc);
}
```

### 원칙

> 두 백엔드가 동일한 RHI 인터페이스를 구현할 때, 한쪽에 있는 방어 코드는
> 다른 쪽에도 동일하게 적용해야 한다.
> `createView()`의 `format = Undefined` fallback은 이제 Vulkan/WebGPU 모두 일치.

---

## 버그 3: skybox.frag.glsl — HDR 없는 기본 모드에서 검은 스카이박스 (시각적 버그)

### 증상

앱 시작 후 스카이박스가 완전히 검은색으로 표시됨.
HDR 환경맵을 로드하면 정상적으로 스카이박스가 표시됨.

### 원인

`skybox.frag.glsl`이 `useEnvironmentMap` 플래그를 무시하고 항상 HDR 큐브맵을 샘플링하도록
수정되어 있었다 (주석: `// Not used anymore - always use HDR`).

```glsl
// 버그 버전 — useEnvironmentMap 무시, 항상 큐브맵 샘플링
void main() {
    vec3 envColor = texture(samplerCube(environmentMap, envSampler), normalize(fragRayDir)).rgb;
    // ...
}
```

`Renderer::createIBL()`은 시작 시 `initializeDefault()`를 호출하여 비어있는 큐브맵(전체 0)을 생성한다.
`setEnvironmentMap()`은 `loadEnvironmentMap()`(HDR 파일 로드 시)에서만 호출된다.

따라서 기본 모드에서:
- `m_hasEnvMap = false` → `useEnvironmentMap = 0` UBO 전달
- 셰이더는 무조건 비어있는(검은) 더미 큐브맵 샘플링 → 검은 스카이박스

### 해결

`useEnvironmentMap` 분기를 복원하고, HDR 없는 경우 절차적(procedural) 하늘 폴백 추가:

```glsl
// 복원된 버전
vec3 proceduralSky(vec3 rayDir, vec3 sunDir) {
    float elev = rayDir.y;
    vec3 zenith  = vec3(0.05, 0.15, 0.40);   // 천정: 짙은 파랑
    vec3 horizon = vec3(0.60, 0.40, 0.25);   // 지평선: 따뜻한 주황
    vec3 ground  = vec3(0.10, 0.08, 0.05);   // 지면: 어두운 갈색

    vec3 sky = (elev > 0.0)
        ? mix(horizon, zenith, sqrt(elev))
        : mix(horizon, ground, clamp(-elev * 4.0, 0.0, 1.0));

    // 태양 디스크 + 글로우
    float s = max(0.0, dot(rayDir, sunDir));
    sky += vec3(1.0, 0.95, 0.8) * (pow(s, 512.0) + pow(s, 8.0) * 0.5);
    return sky;
}

void main() {
    vec3 color;
    if (ubo.useEnvironmentMap != 0) {
        // HDR 큐브맵 경로
        color = texture(samplerCube(environmentMap, envSampler), normalize(fragRayDir)).rgb;
        color *= ubo.exposure;
        color  = ACESFilm(color);
        color  = pow(color, vec3(1.0 / 2.2));
    } else {
        // 절차적 하늘 폴백 (HDR 미로드 시)
        color = proceduralSky(normalize(fragRayDir), normalize(ubo.sunDirection));
        color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));
    }
    outColor = vec4(color, 1.0);
}
```

셰이더 수정 후 SPV 재컴파일:

```bash
glslc -fshader-stage=fragment shaders/skybox.frag.glsl -o shaders/skybox.frag.spv
```

### 원칙

> 셰이더에서 조건 분기를 제거할 때는 해당 조건이 항상 참임을 C++ 쪽에서도 보장해야 한다.
> 기본 모드(HDR 없음)에서도 유효한 폴백 렌더링이 있어야 한다.

---

## 수정된 파일 목록

| 파일 | 버그 유형 | 수정 내용 |
|------|-----------|-----------|
| `src/ui/ImGuiManager.hpp` | 빌드 에러 | Phase 4.1 멤버 선언 및 공개 API 추가 |
| `src/rhi/backends/vulkan/src/VulkanRHITexture.cpp` | 런타임 크래시 | `createView()` Undefined 포맷 fallback 추가 |
| `shaders/skybox.frag.glsl` + `.spv` | 시각적 버그 | 절차적 하늘 폴백 복원 (`useEnvironmentMap` 분기) |

---

## 버그가 숨겨진 이유

이 버그들은 **WASM 빌드 에러**로 인해 네이티브 빌드도 실패하는 동안 드러나지 않았다:

1. `ImGuiManager` 컴파일 에러 → 빌드 자체가 막혀 있어 런타임 버그에 도달 불가
2. WASM 포팅 중 `SkyboxRenderer`에 dummy cubemap 뷰 생성 코드 추가 → Vulkan에서는 `Undefined` 포맷으로 크래시
3. 스카이박스 셰이더에서 절차적 하늘 코드 제거 → HDR 없는 기본 모드에서 검은 화면

WASM 빌드 에러를 수정하고 네이티브 빌드가 정상화되면서 모두 표면으로 드러났다.
