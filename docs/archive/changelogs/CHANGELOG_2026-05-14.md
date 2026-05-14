# 변경 이력 — 2026-05-14

> 작업 범위: 미사용 셰이더 정리 · 문서 구조 재편 · WebGPU 후처리 파이프라인 단일 패스 통합

---

## 1. 개요

세 가지 작업으로 구성:

1. **미사용 셰이더 제거** — 런타임에서 참조하지 않는 `tonemap.frag.glsl` / `tonemap.frag.spv` 파일과 CMakeLists.txt 빌드 항목 제거.

2. **문서 구조 재편** — 진행 중인 문서가 완료된 아카이브 폴더에 묻혀 있던 문제를 해결하기 위해 `docs/`를 `current/`, `roadmap/`, `guides/`, `archive/` 4개 폴더로 재구성.

3. **WebGPU 후처리 파이프라인 단일 패스 통합** — WebGPU 경로에서 Tonemap과 FXAA를 각각 별도 렌더 패스로 처리하던 구조를, SSAO 합성·Bloom 합성·ACES 톤맵·FXAA를 모두 수행하는 단일 셰이더(`postprocess.wgsl`)로 교체. LDR 중간 버퍼(`ldrColorTexture`) 제거.

---

## 2. 미사용 셰이더 정리

### 2.1 제거된 파일

| 파일 | 이유 |
| --- | --- |
| `shaders/tonemap.frag.glsl` | `postprocess.frag.glsl`로 대체됨. 런타임에서 `tonemap.frag.spv`를 로드하는 코드 없음 |
| `shaders/tonemap.frag.spv` | 동일. `Renderer.cpp`는 `postprocess.frag.spv`를 사용 |

### 2.2 CMakeLists.txt 수정

`building_shaders` 타겟에서 `tonemap.frag.spv` 빌드 커맨드 및 의존 항목 제거:

```cmake
# 제거됨
add_custom_command(
    OUTPUT ${BUILDING_SHADER_DIR}/tonemap.frag.spv
    COMMAND ${GLSLC_EXECUTABLE} -fshader-stage=fragment
            -o ${BUILDING_SHADER_DIR}/tonemap.frag.spv
            ${BUILDING_SHADER_DIR}/tonemap.frag.glsl
    ...
)
# building_shaders DEPENDS 목록에서도 제거
```

---

## 3. 문서 구조 재편

### 3.1 변경 전 문제

현재 진행 중인 작업 계획서(`WEBGPU_DEFERRED_PORTING_PLAN.md`)가 이미 완료된 WebGPU 백엔드 구축 기록 폴더(`docs/refactoring/webgpu-backend/`) 안에 있어 탐색이 어려웠음.

### 3.2 새 구조

```text
docs/
├── README.md              ← 전체 네비게이션 인덱스 (재작성)
├── SUMMARY.md / EVOLUTION.md
│
├── current/               ← 현재 진행 중인 작업
│   ├── README.md          ← 진행 상황 인덱스 (신규)
│   └── webgpu-deferred/
│       ├── WEBGPU_DEFERRED_PORTING_PLAN.md   ← 이동
│       └── DEFERRED_RENDERING_TROUBLESHOOTING.md  ← 이동
│
├── roadmap/               ← 거시적 방향 문서
│   ├── SHOWCASE_ROADMAP.md
│   ├── CAREER_ROADMAP.md
│   └── OPTIMIZATION_AND_HARDWARE_ABSTRACTION.md
│
├── guides/                ← 시점 무관 참조 문서
│   ├── BUILD_GUIDE.md
│   └── webgpu/
│
└── archive/               ← 완료된 과거 작업 기록
    ├── refactoring/       (monolith-to-layered, layered-to-rhi, webgpu-backend)
    ├── changelogs/
    ├── debugging/
    └── game_logic/
```

### 3.3 LLM 접근성 개선 효과

- 폴더 이름만으로 역할 판단 가능: `archive/`는 과거 기록, `current/`는 현재 작업

- `docs/current/`만 탐색하면 현재 작업 컨텍스트 즉시 로드 가능

- `current/README.md`에 진행 상황 인덱스를 두어 단일 파일 읽기로 전체 파악 가능

---

## 4. WebGPU 후처리 파이프라인 단일 패스 통합

### 4.1 배경

G-Buffer, Deferred Lighting, SSAO, Bloom이 모두 WebGPU에서 동작하는 상태가 된 이후, 최종 출력 단계의 구조가 다음과 같았음:

```text
HDR buffer → Tonemap pass → LDR buffer (RGBA8Unorm) → FXAA pass → swapchain
```

문제:

- LDR 중간 버퍼(`ldrColorTexture`)가 full-res RGBA8Unorm 텍스처를 추가로 점유
- 렌더 패스를 두 번 인코딩하는 오버헤드
- bloomStrength, exposure, aoStrength 등 파라미터가 셰이더에 상수로 하드코딩되어 런타임 제어 불가
- Vulkan 경로(`postprocess.frag.glsl`)와 달리 WebGPU 경로는 디버그 뷰 전환 기능 없음

### 4.2 신규 셰이더: `shaders/postprocess.wgsl`

기존 `tonemap.wgsl` + `fxaa.wgsl`을 단일 셰이더로 통합. HDR 버퍼·Bloom·SSAO를 입력받아 SSAO 합성 → Bloom 합성 → ACES 톤맵 → 감마 보정 → FXAA를 순서대로 수행한 뒤 swapchain에 직접 출력.

**바인딩 레이아웃:**

| binding | 타입 | 설명 |
| --- | --- | --- |
| 0 | `texture_2d<f32>` | HDR 컬러 버퍼 |
| 1 | `texture_2d<f32>` | Bloom 텍스처 (half-res) |
| 2 | `texture_2d<f32>` | SSAO blur 텍스처 (half-res) |
| 3 | `sampler` | Linear + ClampToEdge |
| 4 | `uniform` | PostProcessParams UBO (32 bytes) |

**PostProcessParams UBO (매 프레임 업데이트):**

```wgsl
struct PostProcessParams {
    bloomStrength : f32,   // 블룸 강도
    exposure      : f32,   // PBR 노출값
    aoStrength    : f32,   // SSAO 강도
    debugView     : i32,   // 0=normal, 7=ssao, 8=bloom
    fxaaOn        : u32,   // FXAA 활성화
    tonemapOn     : u32,   // ACES 톤맵 활성화
    texelW        : f32,   // 1 / width (FXAA 엣지 검출용)
    texelH        : f32,   // 1 / height
}
```

**처리 순서:**

1. SSAO 합성: `composite = hdr * exposure * aoFactor`
2. Bloom 합성: `composite += bloom * bloomStrength`
3. ACES Filmic 톤맵 (조건부)
4. 감마 보정 (`pow(ldr, 1/2.2)`)
5. FXAA (조건부) — HDR 텍스처에서 이웃 픽셀을 샘플링해 톤맵+감마를 적용한 LDR 근사값으로 엣지 검출
6. swapchain에 직접 출력

**FXAA 이웃 픽셀 처리:**

기존 2패스 구조에서는 FXAA가 이미 톤맵된 LDR 텍스처를 샘플링했으나, 단일 패스에서는 LDR 중간 텍스처가 없음. HDR 텍스처를 샘플링해 ACES+감마를 적용한 값으로 luma 기반 엣지 검출 수행. Bloom과 SSAO는 저주파 신호이므로 이웃 픽셀 근사에서 생략해도 시각적 차이 미미.

### 4.3 Renderer.hpp 변경

**제거 (WebGPU 전용 멤버):**

```cpp
std::unique_ptr<rhi::RHITexture>     ldrColorTexture;    // LDR 중간 버퍼
std::unique_ptr<rhi::RHITextureView> ldrColorView;
// Tonemap 파이프라인 관련 6개 멤버 (shader × 2, layout, bindgroup, pipelineLayout, pipeline)
// FXAA 파이프라인 관련 6개 멤버 (동일 구성)
```

**추가 (WebGPU 전용 멤버):**

```cpp
std::unique_ptr<rhi::RHIShader>          wgslPostprocessVertexShader;
std::unique_ptr<rhi::RHIShader>          wgslPostprocessFragmentShader;
std::unique_ptr<rhi::RHIBuffer>          wgslPostprocessParamsUBO;   // 32 bytes
std::unique_ptr<rhi::RHIBindGroupLayout> wgslPostprocessLayout;
std::unique_ptr<rhi::RHIBindGroup>       wgslPostprocessBG;
std::unique_ptr<rhi::RHIPipelineLayout>  wgslPostprocessPipelineLayout;
std::unique_ptr<rhi::RHIRenderPipeline>  wgslPostprocessPipeline;
```

함수 선언 교체:

```cpp
// 제거됨
void createTonemapPipeline();
void createFXAAPipeline();

// 추가됨
void createPostProcessPipelineWGSL();
```

### 4.4 Renderer.cpp 변경

| 위치 | 변경 내용 |
| --- | --- |
| 초기화 블록 | `createTonemapPipeline()` + `createFXAAPipeline()` → `createPostProcessPipelineWGSL()` |
| `createHDRRenderTarget()` | `ldrColorTexture` 생성 블록 제거 |
| `recreateSwapchain()` | tonemap/fxaa bind group 재생성 → `wgslPostprocessBG` 재생성 |
| `drawFrame()` — colorAttachment | WebGPU/Vulkan 분기(`#ifdef __EMSCRIPTEN__`) 제거 — 두 경로 동일해짐 |
| `drawFrame()` — 후처리 | Tonemap pass + FXAA pass (2패스) → PostProcess pass (1패스) |

**drawFrame 후처리 변화:**

```cpp
// 이전: 2패스, LDR 중간 버퍼 경유
if (tonemapPipeline && ...) { /* HDR → ldrColorView */ }
if (fxaaPipeline   && ...) { /* ldrColorView → swapchainView */ }

// 이후: 1패스, 직접 출력
if (wgslPostprocessPipeline && ...) {
    wgslPostprocessParamsUBO->write(&pp, sizeof(pp));  // 런타임 파라미터 주입
    /* HDR → swapchainView (직접) */
}
```

### 4.5 효과

| 항목 | 이전 | 이후 |
| --- | --- | --- |
| 렌더 패스 수 | 2 (tonemap + fxaa) | 1 (통합 postprocess) |
| 중간 텍스처 | ldrColorTexture (RGBA8Unorm, full-res) | 없음 |
| 파라미터 제어 | 셰이더 상수 고정 (BLOOM_STRENGTH = 0.04) | UBO로 런타임 제어 (bloomStrength, exposure, aoStrength, debugView, fxaaOn, tonemapOn) |
| Vulkan 경로와의 기능 동등성 | 부분적 | `postprocess.frag.glsl`과 동등한 파라미터 구조 확보 |

---

## 5. 수정된 파일 목록

| 파일 | 변경 내용 |
| --- | --- |
| `shaders/tonemap.frag.glsl` | **삭제** |
| `shaders/tonemap.frag.spv` | **삭제** |
| `shaders/postprocess.wgsl` | **신규** — 통합 후처리 셰이더 |
| `CMakeLists.txt` | `tonemap.frag.spv` 빌드 커맨드 및 의존 항목 제거 |
| `src/rendering/Renderer.hpp` | WebGPU 후처리 멤버 교체, 함수 선언 교체 |
| `src/rendering/Renderer.cpp` | `createPostProcessPipelineWGSL()` 추가, 기존 tonemap/fxaa 파이프라인 생성 함수 제거, drawFrame 후처리 통합 |
| `docs/README.md` | 새 구조 기반 네비게이션 인덱스로 교체 |
| `docs/current/README.md` | **신규** — 현재 작업 진행 상황 인덱스 |
| `docs/current/webgpu-deferred/` | `WEBGPU_DEFERRED_PORTING_PLAN.md`, `DEFERRED_RENDERING_TROUBLESHOOTING.md` 이동 |
| `docs/roadmap/` | SHOWCASE_ROADMAP, CAREER_ROADMAP, OPTIMIZATION 문서 이동 |
| `docs/guides/` | BUILD_GUIDE, WebGPU 관련 가이드 이동 |
| `docs/archive/` | 완료된 리팩토링 문서, 체인지로그, game_logic, debugging 이동 |
