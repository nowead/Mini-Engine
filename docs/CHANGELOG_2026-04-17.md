# 변경 이력 — 2026-04-17

> 작업 범위: Showcase Demo UI 전면 재설계 · G-Buffer Inspector · GPU 패스 타이머 Bar Chart · Bindless & VMA 메트릭스 패널 · MiniEngine UI 동기화 · CSM 그림자 버그 수정

---

## 1. 개요

두 가지 실행 파일(MiniEngine, showcase_demo) 모두가 구현된 렌더링 기술을 **면접관/엔지니어 관점에서 직관적으로 확인**할 수 있도록 UI를 전면 개편.
동시에 런타임 중 발생하던 **카메라 이동 시 그림자 오위치** 버그를 수정.

주요 작업 세 축:

1. **UI 재설계** — 한국어·Phase X 레이블 제거, 영문 기술 설명으로 교체 (showcase_demo → MiniEngine으로 동기화)
2. **런타임 계측 패널** — GPU 패스별 타이머 Bar Chart + Bindless/VMA 메모리 메트릭스
3. **CSM 버그 수정** — `computeCascadeMatrix` Y-flip 오류 완전 재작성

---

## 2. Showcase Demo UI 전면 재설계

**파일:** `tests/showcase_demo.cpp`

### 2.1 변경 전/후

| 항목 | 이전 | 수정 후 |
|---|---|---|
| 창 제목 | 알 수 없는 이름 | `"Mini-Engine  \|  Vulkan Rendering Showcase"` |
| 섹션 이름 | Phase 1, Phase 2, Phase 3... | 기술 명칭 (Deferred Lighting, 4-Cascade Shadow Maps 등) |
| 언어 | 한국어 포함 ('?' 렌더링) | 전체 영문 |
| 창 너비 | 가변 | 360 px 고정, 알파 0.88 |

### 2.2 섹션 구성

```
Feature Summary           (배지 6개: 핵심 기술 한눈에)
Deferred Lighting         (포인트 라이트 슬라이더 + Forward vs Deferred 비용 비교)
4-Cascade Shadow Maps     (Cascade 범위 시각화 토글 + C0~C3 색상 레이블)
Post-Process Stack        (Bloom / SSAO / ACES / FXAA 개별 On/Off + 인라인 슬라이더)
G-Buffer Inspector        (9채널 라디오버튼 + 뷰별 contextual hint)
Directional Light         (Intensity / Ambient / Exposure)
Bindless Textures & GPU Memory
GPU Pass Timings
FPS / Frame time
Controls
```

### 2.3 G-Buffer Contextual Hints

각 디버그 뷰 선택 시 하단에 설명 텍스트 표시:

| 뷰 | 힌트 |
|---|---|
| Normals | `World-space normals remapped: XYZ -> RGB  (gray = 0.5)` |
| Albedo | `Linear albedo (sRGB conversion handled by swapchain format)` |
| Metallic | `Vertical gradient: row 0 = Metallic 0.0, row 7 = Metallic 1.0` |
| Roughness | `Horizontal gradient: col 0 = Roughness 0.0, col 7 = Roughness 1.0` |
| Depth | `Linearized depth  (near=0.1 m, far=1000 m) -> [0, 1]` |
| SSAO Mask | `Screen-space ambient occlusion mask  (white = fully lit)` |
| Bloom Mask | `Bloom bright-pass mask  (amplified x8 for visibility)` |

---

## 3. G-Buffer Debug View 시스템

9개 채널을 UI에서 실시간 전환.

### 3.1 UBO 레이아웃 변경

**파일:** `src/utils/Vertex.hpp`

```cpp
// 이전
float _pad1 = 0;

// 수정
int debugView = 0;  // 0=Final, 1=Normals, 2=Albedo, 3=Metallic, 4=Roughness,
                    // 5=AO, 6=Depth, 7=SSAO Mask, 8=Bloom Mask
```

동일 4바이트 — std140 레이아웃 변경 없음.

### 3.2 Deferred Lighting 셰이더

**파일:** `shaders/deferred_lighting.frag.glsl`

G-Buffer 샘플링 직후, PBR 계산 전 early-return 블록 삽입:

```glsl
if (ubo.debugView != 0) {
    vec3 dbgColor;
    if      (ubo.debugView == 1) dbgColor = N * 0.5 + 0.5;      // normals
    else if (ubo.debugView == 2) dbgColor = albedo;              // albedo
    else if (ubo.debugView == 3) dbgColor = vec3(metallic);      // metallic
    else if (ubo.debugView == 4) dbgColor = vec3(roughness);     // roughness
    else if (ubo.debugView == 5) dbgColor = vec3(ao);            // material AO
    else { /* view 6: linearized depth */ }
    outColor = vec4(dbgColor, 1.0);
    return;
}
```

뷰 7 (SSAO Mask), 뷰 8 (Bloom Mask)은 해당 텍스처가 바인딩된 `postprocess.frag.glsl`에서 처리.

### 3.3 Postprocess 셰이더 확장

**파일:** `shaders/postprocess.frag.glsl`

Push constant 6개 → 8개 (`debugViewF`, `fxaaEnabled` 추가):

```glsl
layout(push_constant) uniform PC {
    float texelW, texelH;
    float bloomStr, exposure, aoStr;
    float tonemapOn, debugViewF, fxaaOn;
} pc;
```

디버그 뷰별 처리:
- 뷰 1~6: HDR 패스스루 (deferred_lighting에서 이미 기록)
- 뷰 7: `ssaoTexture.rrr`
- 뷰 8: `bloomTexture * 8`
- `fxaaOn < 0.5`: FXAA 스킵, tonemapped 값 직접 출력

### 3.4 Renderer API 추가

**파일:** `src/rendering/Renderer.hpp/.cpp`

```cpp
void setFXAAEnabled(bool e) { fxaaEnabled = e; }
bool getFXAAEnabled() const { return fxaaEnabled; }
void setDebugView(int v)    { debugView = v; }
int  getDebugView() const   { return debugView; }
```

`drawFrame()`의 PostProcessPC 구조체 확장 (6 float → 8 float), 파이프라인 push constant 범위 갱신.

---

## 4. CSM Cascade Visualization

### 4.1 LightingSettings 확장

**파일:** `src/ui/ImGuiManager.hpp`

```cpp
struct LightingSettings {
    // ...기존 필드...
    bool debugCascades  = false;
    bool enableBloom    = true;
    bool enableSSAO     = true;
    bool enableFXAA     = true;
    bool enableTonemap  = true;
    int  debugView      = 0;
};
```

### 4.2 Cascade 색상 레이블

**파일:** `src/ui/ImGuiManager.cpp`, `tests/showcase_demo.cpp`

```
C0 (Red)    =   0 – 10 m   (near, high res)
C1 (Green)  =  10 – 50 m
C2 (Blue)   =  50 – 200 m
C3 (Yellow) = 200 – 1000 m (far)
```

---

## 5. GPU Pass Timer Bar Chart (Task 3)

### 5.1 GpuProfiler TimerId 확장

**파일:** `src/utils/GpuProfiler.hpp`

| 이전 (3개) | 수정 후 (7개) |
|---|---|
| `FrustumCulling = 0` | `FrustumCulling = 0` |
| `ShadowPass = 1` | `ShadowPass = 1` |
| `MainRenderPass = 2` | `GBufferPass = 2` (이름 변경) |
| `Count = 3` | `SSAOPass = 3` (신규) |
| | `BloomPass = 4` (신규) |
| | `DeferredLighting = 5` (신규) |
| | `PostProcess = 6` (신규) |
| | `Count = 7` |

`QUERIES_PER_FRAME = Count * 2`로 constexpr 자동 계산 — VkQueryPool 크기 자동 확장.

### 5.2 Render Graph 람다 계측

**파일:** `src/rendering/Renderer.cpp`

각 Render Graph 람다 내부에 `beginTimer`/`endTimer` 삽입:

| 패스 | Timer | 위치 |
|---|---|---|
| SSAO compute | `SSAOPass` begin | SSAO 람다 시작 |
| SSAOBlur compute | `SSAOPass` end | SSAOBlur 람다 끝 |
| DeferredLighting render | `DeferredLighting` begin/end | 람다 전/후 |
| BloomThreshold compute | `BloomPass` begin | 람다 시작 |
| BloomBlur (iter==3) compute | `BloomPass` end | 마지막 블러 끝 |
| PostProcess render | `PostProcess` begin/end | 람다 전/후 |

타이머는 `dynamic_cast<VulkanRHICommandEncoder*>(enc)->getCommandBuffer()`로 VkCommandBuffer 접근.

### 5.3 Statistics 패널 — Bar Chart

**파일:** `src/ui/ImGuiManager.cpp`

텍스트 수치 나열 → 컬러 ProgressBar 차트로 교체:

```
Frustum Cull   [████░░░░░░░░░░░]  0.12 ms  (녹색)
Shadow Pass    [████████░░░░░░░]  0.45 ms  (파란색)
G-Buffer       [████████████░░░]  0.80 ms  (노란색)
SSAO           [████░░░░░░░░░░░]  0.23 ms  (보라색)
Bloom          [████░░░░░░░░░░░]  0.18 ms  (주황색)
Deferred Lit.  [███░░░░░░░░░░░░]  0.09 ms  (빨간색)
Post-Process   [████░░░░░░░░░░░]  0.15 ms  (청록색)
```

총합 기준 비율로 bar 길이 결정. `GPU Total: X.XX ms  (XXX FPS budget)` 헤더 표시.

### 5.4 Application.cpp 연동

```cpp
gpuTiming.gbufferMs     = profiler->getElapsedMs(GpuProfiler::TimerId::GBufferPass);
gpuTiming.ssaoMs        = profiler->getElapsedMs(GpuProfiler::TimerId::SSAOPass);
gpuTiming.bloomMs       = profiler->getElapsedMs(GpuProfiler::TimerId::BloomPass);
gpuTiming.deferredMs    = profiler->getElapsedMs(GpuProfiler::TimerId::DeferredLighting);
gpuTiming.postprocessMs = profiler->getElapsedMs(GpuProfiler::TimerId::PostProcess);
```

---

## 6. Bindless & VMA 메모리 메트릭스 패널 (Task 8)

### 6.1 BindlessTextureManager 노출

**파일:** `src/rendering/BindlessTextureManager.hpp`

```cpp
uint32_t getRegisteredCount() const { return m_nextIndex; }
```

### 6.2 Renderer::BindlessMetrics

**파일:** `src/rendering/Renderer.hpp/.cpp`

```cpp
struct BindlessMetrics {
    bool     bindlessAvailable  = false;
    uint32_t registeredTextures = 0;   // 현재 등록된 텍스처 수
    uint32_t maxTextures        = 0;   // MAX_TEXTURES = 4096
    uint32_t lastInstanceCount  = 0;   // 마지막 프레임 오브젝트 수
    uint64_t vmaAllocCount      = 0;   // VMA 총 할당 수
    uint64_t vmaAllocatedBytes  = 0;   // 실제 사용 바이트
    uint64_t vmaReservedBytes   = 0;   // 예약 블록 바이트
};
BindlessMetrics getBindlessMetrics() const;
```

`getBindlessMetrics()` 구현: `VulkanRHIDevice::getVmaAllocator()`에서 VmaAllocator 획득 → `vmaCalculateStatistics()`로 전체 힙 통계 쿼리.

### 6.3 Descriptor Bind 절감 시각화

```
Bindless bind:     1  (global descriptor array)
Traditional bind:  N  (objects × textures)
[█░░░░░░░░░░░░░░░]  bindless ratio
```

N = `lastInstanceCount × registeredTextures`. ProgressBar 너비 = 1/N (bindless가 얼마나 적은 bind를 사용하는지 직관적 표현).

### 6.4 VMA 메모리 Bar

```
VMA: 247 allocs  |  312.4 MB used  |  384.0 MB reserved
[████████████░░░░]  312.4 / 384.0 MB
```

---

## 7. MiniEngine 실행파일 UI 동기화

**파일:** `src/ui/ImGuiManager.cpp`

showcase_demo와 동일한 정보를 MiniEngine 실행파일에도 표시하도록 `renderUI()` 전면 재작성.

### 7.1 추가된 항목

| 항목 | 내용 |
|---|---|
| 창 제목 | `"Mini-Engine  \|  Vulkan Deferred Renderer  \|  PBR + CSM + Bindless"` |
| 창 너비 | 390 px 고정, 알파 0.88 |
| Feature Summary | 배지 6개 (항상 표시) |
| 컬러 섹션 헤더 | `PushStyleColor(Text, 파란색)`로 G-Buffer/CSM/Post-Process 섹션 강조 |
| G-Buffer Inspector | 9채널 + contextual hint (showcase_demo와 동일) |
| CSM Cascade | 범위 레이블 C0~C3 (0-10m / 10-50m / 50-200m / 200-1000m) |
| Post-Process | 서술적 레이블 ("Kawase Dual Blur", "Bilateral Blur", "ACES Filmic Tonemap", "Luminance edge detection") |
| ACES Off 경고 | `[!] Raw HDR output — highlights clipped` |

### 7.2 섹션 구조 개편

G-Buffer Inspector, 4-Cascade Shadow Maps, Post-Process Stack을 기존 `Lighting` CollapsingHeader에서 분리하여 **항상 보이는 독립 섹션**으로 변경.

Lighting 상세 설정 (Azimuth/Elevation, 프리셋, Shadow Bias 등)은 `Directional Light / Exposure` CollapsingHeader로 이동.

---

## 8. CSM 그림자 버그 수정

**파일:** `src/rendering/ShadowRenderer.cpp`

### 8.1 버그 원인

`computeCascadeMatrix`에서 카메라 projection matrix의 [1][1] 원소로 fov를 추출:

```cpp
// 버그 코드
float f    = cameraProj[1][1];   // Vulkan Y-flip으로 인해 음수!
float fovY = 2.0f * std::atan(1.0f / f);  // 음수 fov → 쓰레기 행렬
```

`Camera::getProjectionMatrix()`는 Vulkan NDC를 위해 `proj[1][1] *= -1`을 적용하므로, [1][1]이 음수. 이로 인해:
- aspect 비율 음수
- fovY 음수 → `glm::perspective(음수, 음수, ...)` → 쓰레기 sub-projection
- frustum 코너 완전 오계산 → 카메라 이동 시 그림자 오위치

### 8.2 수정 방법

**Sub-projection 재구성 방식 완전 폐기**, VP 역행렬 unproject + 선형 보간 방식으로 교체:

```
Step 1: proj[2][2], proj[3][2]로 cameraNear/cameraFar 추출
        (Y-flip은 [2][2], [3][2]에 영향 없음)
        near = B / (A - 1),  far = B / (A + 1)
        where A = proj[2][2], B = proj[3][2]

Step 2: inv(P * V)로 8개 NDC 코너 (z=0, z=1) → world space unproject

Step 3: cameraNear~cameraFar 구간에서 nearSlice~farSlice로 선형 보간
        → cascade sub-frustum 8코너

Step 4: 코너들의 light-space AABB 계산
        Z 방향 확장 (50%): 뷰 프러스텀 밖 그림자 캐스터 포착
        XY 방향 3% 마진: 엣지 아티팩트 방지

Step 5: glm::ortho로 light projection 생성 + Vulkan [0,1] depth 변환
```

### 8.3 수정 전/후 비교

| 항목 | 이전 | 수정 후 |
|---|---|---|
| fov 추출 | `proj[1][1]` (Y-flip 영향) | `proj[2][2]`, `proj[3][2]` (영향 없음) |
| frustum 코너 | 잘못된 sub-projection invert | 실제 VP 역행렬로 직접 unproject |
| sub-slice 분리 | 별도 perspective 행렬 재구성 | near/far 사이 선형 보간 |
| 카메라 이동 시 | 그림자 오위치 | 올바른 cascade 추적 |

---

## 9. 수정된 파일 목록

| 파일 | 변경 내용 |
|---|---|
| `src/utils/Vertex.hpp` | `_pad1` → `int debugView` |
| `src/utils/GpuProfiler.hpp` | TimerId 3→7개, TIMER_NAMES 갱신 |
| `src/rendering/ShadowRenderer.cpp` | `computeCascadeMatrix` 전면 재작성 |
| `src/rendering/BindlessTextureManager.hpp` | `getRegisteredCount()` 추가 |
| `src/rendering/Renderer.hpp` | `BindlessMetrics`, `fxaaEnabled`, `debugView`, `lastInstanceCount` |
| `src/rendering/Renderer.cpp` | GPU 타이머 람다 계측, `getBindlessMetrics()`, `lastInstanceCount` 캐시 |
| `src/ui/ImGuiManager.hpp` | `LightingSettings` 확장, `BindlessMetrics`, `GPUTiming` 7필드 |
| `src/ui/ImGuiManager.cpp` | `renderUI()` 전면 재작성 |
| `src/Application.cpp` | GPU 타이머 7개 연동, Bindless 메트릭스 연동 |
| `shaders/deferred_lighting.frag.glsl` | debugView early-return 블록 |
| `shaders/postprocess.frag.glsl` | push constant 8개, debug view 라우팅 |
| `tests/showcase_demo.cpp` | UI 전면 재설계, Bindless/GPU 패널 추가 |
| `docs/SHOWCASE_ROADMAP.md` | 신규 생성: Task 1~8 계획 + 면접 데모 스크립트 |

---

## 10. 로드맵 진행 현황

| Task / Phase | 내용 | 상태 |
|---|---|---|
| Task 1 | G-Buffer Debug Viewer (9채널) | ✅ 완료 |
| Task 2 | CSM Cascade Visualization | ✅ 완료 |
| Task 3 | GPU Pass Timer Bar Chart (7 passes) | ✅ 완료 |
| Task 4 | Post-Process 개별 On/Off 토글 | ✅ 완료 |
| Task 8 | Bindless & VMA 메모리 메트릭스 | ✅ 완료 |
| UI 동기화 | MiniEngine ↔ showcase_demo 동일 정보 | ✅ 완료 |
| 버그 수정 | CSM computeCascadeMatrix Y-flip 오류 | ✅ 완료 |
| Phase 5 | 센서 시뮬레이션 (LiDAR / 카메라 노이즈) | 미착수 |
