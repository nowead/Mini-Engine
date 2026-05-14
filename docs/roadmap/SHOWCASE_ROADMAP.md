# Showcase 강화 로드맵

> 작성일: 2026-04-16  
> 목표: Phase 1~4의 기술 역량을 면접관이 30분 안에 직접 확인할 수 있는 인터랙티브 데모로 재구성  
> 전제: 새 피처 추가 없이 기존 구현을 **보이게** 만드는 작업

---

## 현황 진단 — 지금 쇼케이스의 문제

### 기술적으로 구현되어 있지만 보이지 않는 것들

| 구현 완료 피처 | 현재 UI 노출 | 문제 |
|---|---|---|
| Render Graph (자동 배리어 추론) | 없음 | "어떻게 동작하는지 보여줄 수 없다" |
| G-Buffer Deferred Rendering | 없음 | 면접관이 GBuffer가 뭔지 직접 볼 수 없다 |
| 4-Cascade CSM | 없음 | 단일 맵과 4-cascade의 차이를 시연 불가 |
| Bindless Texture (4096 슬롯) | 없음 | 드로우 콜 절감 효과가 숫자로 안 나온다 |
| SSAO | 강도 슬라이더만 | On/Off 비교가 없어서 효과가 분명하지 않다 |
| Bloom | 강도 슬라이더만 | 동일 문제 |
| GPU Frustum Culling | GPU 타이머만 | 컬링 전/후 오브젝트 수 비교 없음 |
| IBL + PBR | 건물에 적용됨 | 재질 다양성이 없어서 PBR 역량이 안 보임 |

### 씬 수준의 문제

현재 씬은 동일한 회색 박스가 격자로 나열된 구조다.  
- 재질 다양성 없음 → PBR 효과 식별 불가  
- 점 광원 없음 → Deferred Rendering의 핵심 장점(다수 동적 광원) 미시연  
- 시각적 맥락 없음 → 자율주행 시뮬레이션 도메인과의 연결이 약함  

---

## 전략: 두 가지 강화 축

```
축 1: 씬 강화     — 기술이 돋보이는 시각적 환경 구축
축 2: UI 강화     — 피처를 On/Off/Compare/Debug할 수 있는 인터랙티브 패널
```

두 축을 동시에 진행하되 의존성은 없다. 작업 단위별로 독립적으로 머지 가능.

---

## Showcase Task 1 — G-Buffer 디버그 뷰어

**연결 피처:** Phase 3 Deferred Rendering  
**면접 가치:** G-Buffer를 직접 보여주며 Deferred 파이프라인의 데이터 흐름 설명 가능

### 구현

`ImGuiManager`에 디버그 뷰 열거형 추가:

```cpp
enum class DebugView {
    None,         // 정상 출력
    GBuffer0,     // 노멀(XYZ) + Roughness
    GBuffer1,     // 알베도(RGB) + Metallic
    GBuffer2,     // Emissive + AO
    Depth,        // 선형화 깊이
    ShadowMap,    // CSM 최상위 캐스케이드
    SSAO,         // SSAO 마스크 단독
    BloomMask,    // Bloom 임계값 이상 픽셀
};
```

`shaders/tonemap.frag.glsl`에 `debugView` push constant 추가:

```glsl
layout(push_constant) uniform PushConstants {
    float exposure;
    float bloomStrength;
    int   debugView;   // DebugView enum value
};

// debugView != 0 이면 해당 G-Buffer 채널만 fullscreen quad에 출력
```

**ImGui 패널:**

```
[Debug Views]
○ Final     ○ Normals    ○ Albedo
○ Emissive  ○ Depth      ○ SSAO
○ Bloom Mask  ○ Shadow Map
```

**수정 파일:**
- `src/ui/ImGuiManager.hpp/.cpp`: `DebugView` 열거형, UI 라디오 버튼
- `src/rendering/Renderer.hpp/.cpp`: `setDebugView(DebugView)` API
- `shaders/tonemap.frag.glsl`: `debugView` 분기 추가
- `shaders/tonemap.vert.glsl`: push constant 범위 확장

---

## Showcase Task 2 — CSM 캐스케이드 시각화

**연결 피처:** Phase 3 Cascade Shadow Maps  
**면접 가치:** "어떤 거리에서 어떤 캐스케이드가 사용되는가"를 색상으로 즉시 설명

### 구현

`shaders/deferred_lighting.frag.glsl`에 `cascadeDebug` 모드 추가:

```glsl
// cascadeDebug == true 일 때
// cascade 0 (0-10m)  : 빨강 오버레이
// cascade 1 (10-50m) : 초록 오버레이
// cascade 2 (50-200m): 파랑 오버레이
// cascade 3 (200m+)  : 노랑 오버레이
vec3 cascadeColors[4] = {
    vec3(1.0, 0.2, 0.2),
    vec3(0.2, 1.0, 0.2),
    vec3(0.2, 0.4, 1.0),
    vec3(1.0, 1.0, 0.2)
};
if (cascadeDebug) {
    finalColor = mix(finalColor, cascadeColors[selectedCascade], 0.4);
}
```

**ImGui 패널 추가 항목:**

```
[Shadows]
□ Show Cascade Regions        ← 캐스케이드 색상 오버레이
  Cascade 0:  0 –  10 m  [빨강]
  Cascade 1: 10 –  50 m  [초록]
  Cascade 2: 50 – 200 m  [파랑]
  Cascade 3: 200 – 1000m [노랑]
Split Lambda: [======] 0.75
```

**수정 파일:**
- `shaders/deferred_lighting.frag.glsl`: `cascadeDebug` uniform + 색상 오버레이
- `src/rendering/DeferredLightingPass.hpp/.cpp`: uniform 전달
- `src/ui/ImGuiManager.hpp/.cpp`: 체크박스 + 범례 텍스트

---

## Showcase Task 3 — Render Graph 패스 타이머 패널

**연결 피처:** Phase 2 Render Graph  
**면접 가치:** "Render Graph가 패스를 어떤 순서로 실행하고 각각 얼마나 걸리는가"를 실시간 수치로 제시

### 현황

현재 `GpuProfiler`는 `FrustumCulling`, `ShadowPass`, `MainRenderPass` 3개 타이머만 노출.  
Render Graph의 전체 패스 목록이 UI에 드러나지 않는다.

### 구현

`GpuProfiler::TimerId`에 누락된 패스 추가:

```cpp
enum class TimerId : uint32_t {
    FrustumCulling = 0,
    ShadowPass,
    GBufferPass,      // 추가
    SSAOPass,         // 추가
    BloomThreshold,   // 추가
    BloomBlur,        // 추가
    DeferredLighting, // 추가
    TonemapFXAA,      // 추가
    Count
};
```

**ImGui 통계 패널을 가로 바 차트로 교체:**

```
[Render Graph — Pass Timing]
FrustumCull    ▓▓░░░░░░░░░░░░░░  0.12 ms
ShadowPass     ▓▓▓▓▓░░░░░░░░░░░  0.47 ms
GBuffer        ▓▓▓▓▓▓▓░░░░░░░░░  0.61 ms
SSAO           ▓▓░░░░░░░░░░░░░░  0.18 ms
BloomThreshold ▓░░░░░░░░░░░░░░░  0.08 ms
BloomBlur      ▓▓░░░░░░░░░░░░░░  0.11 ms
DeferredLight  ▓▓▓░░░░░░░░░░░░░  0.29 ms
Tonemap+FXAA   ▓░░░░░░░░░░░░░░░  0.05 ms
─────────────────────────────────
GPU Total                         1.91 ms
```

ImGui `ImGui::PlotHistogram` 또는 직접 `ImDrawList` 로 바 렌더링.

**수정 파일:**
- `src/utils/GpuProfiler.hpp`: `TimerId` 열거값 추가
- `src/rendering/Renderer.cpp`: 각 패스 begin/end 타이머 삽입
- `src/ui/ImGuiManager.hpp/.cpp`: `GPUTiming` 구조체 확장, 바 차트 렌더링

---

## Showcase Task 4 — 포스트 프로세스 On/Off 비교 모드

**연결 피처:** Phase 1 Post-Processing (SSAO / Bloom / FXAA / Tonemap)  
**면접 가치:** 효과가 있을 때와 없을 때를 즉시 토글하여 차이를 명확히 시연

### 구현

`LightingSettings`에 bool 플래그 추가:

```cpp
struct LightingSettings {
    // 기존 ...
    bool enableSSAO   = true;
    bool enableBloom  = true;
    bool enableFXAA   = true;
    bool enableTonemap = true;  // false시 선형 출력
};
```

`shaders/tonemap.frag.glsl`에 `featureFlags` 비트 필드:

```glsl
layout(push_constant) uniform PC {
    float exposure;
    float bloomStrength;
    float aoStrength;
    int   debugView;
    uint  featureFlags;  // bit0=SSAO, bit1=Bloom, bit2=FXAA, bit3=Tonemap
};
```

**ImGui 패널:**

```
[Post-Process Stack]
☑ SSAO      [strength: ====] 0.6
☑ Bloom     [strength: ====] 0.04
☑ FXAA
☑ Tonemap (ACES Filmic)
              [모두 켜기] [모두 끄기]
```

각 체크박스를 끄면 해당 패스는 바이패스되며 GPU 시간이 0에 가까워지는 것이 통계 패널에 즉시 반영된다.

**수정 파일:**
- `src/rendering/Renderer.hpp/.cpp`: `setFeatureFlags(uint32_t)` API
- `shaders/tonemap.frag.glsl`: `featureFlags` 분기
- `shaders/ssao.comp.glsl`: early-exit 분기 (호스트에서 디스패치 자체를 건너뜀이 더 효율적)
- `src/ui/ImGuiManager.hpp/.cpp`: 체크박스 추가

---

## Showcase Task 5 — 재질 다양성 & 동적 점 광원 씬

**연결 피처:** Phase 4 Bindless + Phase 3 Deferred Multi-Light  
**면접 가치:** "Deferred는 왜 좋은가" → 점 광원을 20개 켜도 FPS가 유지되는 것을 실시간 수치로 증명

### Task 5-A: 건물 재질 3종 분류

현재 모든 건물이 동일 재질. 높이 구간으로 재질 자동 분류:

| 높이 구간 | 재질 | 외관 |
|---|---|---|
| 0 – 30m  | `Concrete`  | Roughness 0.8 / Metallic 0.0 |
| 30 – 80m | `Glass`     | Roughness 0.1 / Metallic 0.0 / 반사 강조 |
| 80m+     | `Metal`     | Roughness 0.3 / Metallic 1.0 |

각 재질은 ProceduralTexture (컴퓨트 셰이더로 GPU에서 직접 생성, 외부 파일 의존성 없음):
- `Concrete`: 노이즈 기반 회색 벽면 + 수평 줄 패턴
- `Glass`: 반사 격자 패턴 (청색 + 흰색)
- `Metal`: 패널 경계선 + 금속 광택

신규 파일:
```
src/rendering/ProceduralTextureGen.hpp/.cpp
shaders/proc_concrete.comp.glsl
shaders/proc_glass.comp.glsl
shaders/proc_metal.comp.glsl
```

`BindlessTextureManager`에 등록 후 건물 인스턴스 데이터의 `textureIndex` 필드 활용 → Bindless가 실제로 동작하는 시각적 증거.

### Task 5-B: 동적 점 광원 20개

`DeferredLightingPass`가 이미 점 광원 리스트를 받을 수 있는 구조. 도로 교차점 위치에 가로등 광원 배치:

```cpp
// Application::initGameLogic()에 추가
std::vector<PointLight> streetLights;
for (int x = 0; x < gridSize - 1; x++) {
    for (int z = 0; z < gridSize - 1; z++) {
        PointLight light;
        light.position  = glm::vec3(startX + (x + 0.5f) * spacing, 8.0f, startZ + (z + 0.5f) * spacing);
        light.color     = glm::vec3(1.0f, 0.9f, 0.7f);  // 따뜻한 가로등 색
        light.intensity = 50.0f;
        light.radius    = 25.0f;
        streetLights.push_back(light);
    }
}
renderer->setPointLights(streetLights);
```

**ImGui 패널 추가:**

```
[Lighting]
Point Lights: [====] 20 (active)
□ Animate Lights     ← 광원 강도 맥동 애니메이션
[Add Light] [Remove Light]
```

Deferred의 장점이 숫자로 보임: 점 광원 1개 vs 20개에서 프레임 시간 변화가 Forward에 비해 선형적이 아님.

**수정 파일:**
- `src/rendering/Renderer.hpp/.cpp`: `setPointLights(vector<PointLight>)` API
- `src/rendering/DeferredLightingPass.hpp/.cpp`: 점 광원 SSBO 업로드
- `shaders/deferred_lighting.frag.glsl`: 점 광원 루프 확장
- `src/ui/ImGuiManager.hpp/.cpp`: 점 광원 수 슬라이더

---

## Showcase Task 6 — Frustum Culling 시각화

**연결 피처:** 기존 GPU Frustum Culling (Compute + Indirect Draw)  
**면접 가치:** "컬링이 실제로 동작하는가"를 오브젝트 수로 즉시 증명

### 구현

`frustum_cull.comp.glsl`의 `visibleCount` 어토믹 카운터를 호스트에서 리드백:

```cpp
// 현재 인다이렉트 버퍼만 GPU에 존재
// → 1 uint 크기의 readback 버퍼 추가
uint32_t visibleCount = renderer->getVisibleObjectCount();  // 매 프레임 poll
```

**ImGui 통계 패널 보강:**

```
[Culling Statistics]
Total Objects:   1,000
Visible (drawn): 423     ← 컬링 후 실제 드로우 수
Culled:          577  (57.7%)
GPU Cull Time:   0.12 ms

□ Show Frustum Wireframe   ← 카메라 절두체를 씬에 오버레이
```

절두체 와이어프레임은 6개 면을 `VK_PRIMITIVE_TOPOLOGY_LINE_LIST`로 렌더링. 카메라를 회전하면 오브젝트가 절두체 밖으로 나가며 Culled 수가 증가하는 것이 실시간으로 보임.

**수정 파일:**
- `src/rendering/Renderer.hpp/.cpp`: `getVisibleObjectCount()`, readback 버퍼
- `src/ui/ImGuiManager.hpp/.cpp`: 컬링 통계 패널
- 절두체 와이어프레임: `src/rendering/DebugRenderer.hpp/.cpp` (신규, 간단한 선 렌더러)

---

## Showcase Task 7 — 씬 모드 전환 시스템

**목표:** 하나의 실행 파일에서 여러 "데모 씬"을 키보드로 전환  
**면접 활용:** 면접 중 "이 피처 보여드릴게요" → 즉시 해당 씬으로 전환

### 씬 목록

| 키 | 씬 이름 | 강조 피처 |
|---|---|---|
| `1` | **PBR Material Gallery** | 재질 3종 × 구 배열. IBL 반사, Metallic/Roughness 파라미터 비교 |
| `2` | **Urban Block** | 4×4 건물 그리드, 가로등 20개, 일몰 → 야간 시간 경과 |
| `3` | **Stress Test** | 100K 오브젝트, GPU 컬링 비율 실시간 표시 |
| `4` | **Shadow Showcase** | CSM 캐스케이드 색상 오버레이 자동 활성화, 카메라 느린 이동으로 cascade 경계 crossing 시연 |
| `5` | **G-Buffer Tour** | 디버그 뷰 자동 순환 (1초마다 GBuffer0 → GBuffer1 → GBuffer2 → Depth → Final) |

씬 전환 시 카메라 위치·조명 프리셋도 함께 복원.

**신규 파일:**
```
src/showcase/ShowcaseManager.hpp/.cpp
src/showcase/scenes/PBRGalleryScene.hpp/.cpp
src/showcase/scenes/UrbanBlockScene.hpp/.cpp
src/showcase/scenes/StressTestScene.hpp/.cpp
src/showcase/scenes/ShadowShowcaseScene.hpp/.cpp
src/showcase/scenes/GBufferTourScene.hpp/.cpp
```

**ImGui 패널 상단에 씬 선택 탭바 추가:**

```
[1: PBR] [2: Urban] [3: Stress] [4: Shadow] [5: G-Buffer]
         ↑ 현재 활성
```

---

## Showcase Task 8 — Bindless 효과 측정 패널

**연결 피처:** Phase 4 Bindless Texture  
**면접 가치:** 수치로 CPU 오버헤드 절감 증명

### 구현

`BindlessTextureManager`에 카운터 추가:

```cpp
struct BindlessStats {
    uint32_t totalTextures;      // 등록된 텍스처 수
    uint32_t uniqueMaterials;    // 유니크 머티리얼 수
    uint32_t descriptorSetBinds; // 기존 방식 시뮬레이션: drawCall당 1
    uint32_t bindlessBinds;      // 실제: 씬 전체 1
    float    estimatedSavingMs;  // 디스크립터 바인딩 절감 추정치
};
```

**ImGui 패널:**

```
[Bindless Textures]
Registered Textures: 3
Unique Materials:    3
─────────────────────────────
Traditional:  1 bind × 1,000 draws = 1,000 descriptor binds
Bindless:     1 bind total
Savings:      999 binds eliminated
Est. CPU gain: ~0.08 ms/frame
```

---

## 구현 우선순위 및 일정 제안

| 순위 | Task | 작업량 | 면접 임팩트 |
|---|---|:---:|:---:|
| **1** | Task 1 — G-Buffer 디버그 뷰 | 소 | 매우 높음 |
| **2** | Task 2 — CSM 캐스케이드 시각화 | 소 | 높음 |
| **3** | Task 4 — 포스트 프로세스 On/Off | 소 | 중간 |
| **4** | Task 3 — Render Graph 패스 타이머 | 중 | 높음 |
| **5** | Task 6 — Frustum Culling 시각화 | 중 | 높음 |
| **6** | Task 5-A — 재질 다양성 (Procedural) | 중 | 높음 |
| **7** | Task 5-B — 동적 점 광원 씬 | 중 | 높음 |
| **8** | Task 7 — 씬 모드 전환 | 대 | 중간 |
| **9** | Task 8 — Bindless 측정 패널 | 소 | 중간 |

### 권장 진행 순서

**1단계 (1~2주):** Task 1 + Task 2 + Task 4  
셰이더 수정 위주. 빌드 후 즉시 효과 확인 가능. 가장 임팩트 높은 작업.

**2단계 (2~3주):** Task 3 + Task 6  
GpuProfiler 확장 + Readback 버퍼. 코드량은 적지만 Vulkan 동기화 주의 필요.

**3단계 (3~5주):** Task 5-A + Task 5-B  
Procedural Texture 생성이 가장 큰 작업. 완성 시 씬이 즉시 시각적으로 업그레이드됨.

**4단계 (5~7주):** Task 7 + Task 8  
씬 전환 시스템은 리팩터링 성격. 다른 Task들이 완성된 후 최종 정리 단계에서 진행.

---

## 면접 시연 스크립트 (Task 완성 후 기준)

### MORAI 면접 — 15분 데모 플로우

```
1. [씬 2: Urban Block] 
   → "도시 규모 씬에서 실시간 렌더링 데모입니다."

2. [조명 → Sunset 프리셋]
   → "조명 환경을 자율주행 테스트 시간대인 일몰로 설정합니다."

3. [Shadows → Show Cascade Regions 체크]
   → "4-Cascade CSM입니다. 빨강=5m, 초록=50m, 파랑=200m, 노랑=1000m."
   → "카메라를 뒤로 빼보면 cascade 경계가 이동하는 것을 확인할 수 있습니다."

4. [Debug Views → SSAO]
   → "G-Buffer 기반 SSAO입니다. 건물 기저부의 암부가 셀프-섀도우로 자연스럽게 표현됩니다."

5. [Debug Views → None, Post-Process → SSAO Off]
   → "SSAO를 끄면 건물 하단이 플랫해집니다. 현실 카메라 센서에서 주변광 차폐가 중요한 이유입니다."

6. [씬 3: Stress Test → 100K]
   → "100,000개 오브젝트에서 GPU Frustum Culling으로 실제 드로우는 42,300개 수준을 유지합니다."
```

### 42dot 면접 — 10분 데모 플로우

```
1. [Render Graph 패스 타이머 패널 확대]
   → "Render Graph가 8개 패스를 의존성 그래프 순서로 실행하며 각 패스 GPU 시간을 측정합니다."

2. [씬 2: Urban Block, 점 광원 20개 활성]
   → "Deferred Rendering이므로 점 광원 수가 증가해도 G-Buffer 패스 시간은 변하지 않습니다."
   → [점 광원 20 → 1로 조절] "광원 1개와 20개의 G-Buffer 시간을 비교해보겠습니다."

3. [Bindless 패널]
   → "1,000개 오브젝트에서 디스크립터 셋 바인딩 999회가 제거되었습니다."
```

---

## 수정/생성 파일 요약

### Task 1 (G-Buffer 뷰어)
| 파일 | 작업 |
|---|---|
| `src/ui/ImGuiManager.hpp/.cpp` | `DebugView` 열거형, 라디오 버튼 패널 |
| `src/rendering/Renderer.hpp/.cpp` | `setDebugView()` API |
| `shaders/tonemap.frag.glsl` | `debugView` push constant 분기 |

### Task 2 (CSM 시각화)
| 파일 | 작업 |
|---|---|
| `shaders/deferred_lighting.frag.glsl` | `cascadeDebug` 색상 오버레이 |
| `src/rendering/DeferredLightingPass.hpp/.cpp` | uniform 전달 |
| `src/ui/ImGuiManager.hpp/.cpp` | 체크박스 + 범례 |

### Task 3 (Render Graph 타이머)
| 파일 | 작업 |
|---|---|
| `src/utils/GpuProfiler.hpp` | `TimerId` 5개 추가 |
| `src/rendering/Renderer.cpp` | 패스별 타이머 삽입 |
| `src/ui/ImGuiManager.hpp/.cpp` | `GPUTiming` 확장, 바 차트 |

### Task 4 (Post-Process 토글)
| 파일 | 작업 |
|---|---|
| `src/rendering/Renderer.hpp/.cpp` | `setFeatureFlags()` |
| `shaders/tonemap.frag.glsl` | `featureFlags` 비트 분기 |
| `src/ui/ImGuiManager.hpp/.cpp` | 체크박스 4개 |

### Task 5 (씬 강화)
| 파일 | 작업 |
|---|---|
| `src/rendering/ProceduralTextureGen.hpp/.cpp` | 신규 생성 |
| `shaders/proc_concrete.comp.glsl` | 신규 생성 |
| `shaders/proc_glass.comp.glsl` | 신규 생성 |
| `shaders/proc_metal.comp.glsl` | 신규 생성 |
| `shaders/deferred_lighting.frag.glsl` | 점 광원 루프 확장 |
| `src/rendering/DeferredLightingPass.hpp/.cpp` | 점 광원 SSBO |
| `src/ui/ImGuiManager.hpp/.cpp` | 점 광원 슬라이더 |

### Task 6 (Frustum Culling 시각화)
| 파일 | 작업 |
|---|---|
| `src/rendering/Renderer.hpp/.cpp` | `getVisibleObjectCount()`, readback 버퍼 |
| `src/rendering/DebugRenderer.hpp/.cpp` | 신규 생성 (선 렌더러) |
| `src/ui/ImGuiManager.hpp/.cpp` | 컬링 통계 패널 |

### Task 7 (씬 모드 전환)
| 파일 | 작업 |
|---|---|
| `src/showcase/ShowcaseManager.hpp/.cpp` | 신규 생성 |
| `src/showcase/scenes/*.hpp/.cpp` | 씬 5종 신규 생성 |
| `src/ui/ImGuiManager.hpp/.cpp` | 탭바 추가 |

### Task 8 (Bindless 측정)
| 파일 | 작업 |
|---|---|
| `src/rendering/BindlessTextureManager.hpp/.cpp` | `BindlessStats` 카운터 |
| `src/ui/ImGuiManager.hpp/.cpp` | 측정 패널 |
