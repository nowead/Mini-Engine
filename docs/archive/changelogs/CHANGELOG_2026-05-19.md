# 변경 이력 — 2026-05-19

> 작업 범위: WebGPU 그림자 전면 재작성(CSM 폐기 → 단일 씬-고정 맵 + PCSS) 및
> 그 과정에서 드러난 셰이더 구조체 스트라이드 불일치 · 빌드 환경 결함 수정

---

## 1. 개요

WebGPU/WASM 빌드에서 그림자가 지속적으로 깨졌다. 증상이 시도할 때마다 조금씩
달라 보였으나 본질은 하나였다:

- 그림자가 건물과 어긋나고 형태·크기·방향이 틀림
- 카메라 줌인/줌아웃·공전 시 그림자가 출렁이고 거대해짐
- 단일 건물의 그림자가 단지 전체를 덮을 만큼 거대
- "Shadow" 디버그 뷰가 frustum 박스 거의 전체를 검게 표시

수많은 수정(좌표 관례, cascade 정밀도, 안정화, 태양각 등)이 **화면에 전혀 반영되지
않는** 현상이 반복됐다. 최종적으로 두 부류의 원인이 확인됐다:

1. **진짜 렌더링 버그** — `shadow.wgsl`의 `ObjectData` 구조체만 다른 셰이더/ C++와
   레이아웃이 달라(144바이트 vs 128바이트) 셰도우 패스가 건물 데이터를 어긋난
   바이트에서 읽었다. shadow map이 쓰레기 지오메트리로 채워졌다.
2. **버그를 가린 빌드 환경 결함 4종** — 코드 수정이 실제 실행 바이너리에 반영되지
   않게 만들어, 올바른 수정조차 "효과 없음"으로 보이게 했다.

이 문서는 결론(2장)과 더불어, 왜 이렇게 오래 걸렸는지(3장 빌드 환경, 4장 진단
여정)를 비교적 상세히 남긴다. 동일한 함정의 재발을 막기 위함이다.

---

## 2. 진짜 근본 원인 — `ObjectData` 구조체 스트라이드 불일치

C++ `ObjectData`(`src/rendering/InstancedRenderData.hpp`)는 **128바이트**다:

```
mat4 worldMatrix      (64)
vec4 boundingBoxMin   (16)
vec4 boundingBoxMax   (16)
vec4 colorAndMetallic (16)
vec4 roughnessAOPad   (16)   → 합계 128
```

`gbuffer.wgsl`과 `frustum_cull.comp.wgsl`의 `ObjectData`는 정확히 위 5필드(128B)로
일치했다. 그러나 `shadow.wgsl`의 `ObjectData`만 **6필드(144B)** 였다 — 끝에
`texParams: vec4<f32>`가 추가돼 있었고, 주석에는 `// C++ ObjectData 144 bytes 일치`
라고 **사실과 반대로** 적혀 있었다.

SSBO `array<ObjectData>`는 스트라이드로 인덱싱된다. 셰도우 패스는 스트라이드 144로,
실제 버퍼는 128로 패킹돼 있었다:

- `objects[0]` — 오프셋 0, 정상(지면 데이터)
- `objects[1]` — 셰이더는 144에서 읽지만 실제는 128 → 16B 어긋남
- `objects[i]` — `16 × i` 바이트 누적 오프셋 → 건물 `worldMatrix`/`boundingBox`가
  전부 쓰레기값

결과: 셰도우 패스가 건물을 말도 안 되는 스케일·위치로 변환해 shadow map에 기록 →
조명 비교 시 거대·왜곡 그림자. GBuffer(메인 화면)는 올바른 구조체라 정상이었고,
그래서 "씬은 멀쩡한데 그림자만" 계속 깨졌다. 이 한 가지 사실이 ortho·cascade·
태양각 등 모든 후속 수정과 **무관하게** 증상을 유지시켰다.

**수정:** `shaders/shadow.wgsl`의 `ObjectData`에서 `texParams` 줄 제거 → 128B로
C++/다른 셰이더와 일치. 재발 방지를 위해 "C++/gbuffer/frustum_cull과 반드시 일치"
주석을 명시.

---

## 3. 버그를 가린 빌드 환경 결함 4종

올바른 수정이 화면에 반영되지 않은 이유. 진단 루프를 마비시킨 핵심 요인들이다.

### 3.1 NMake 증분 빌드가 소스 변경을 미감지

`.wgsl`/`.html`은 링크 시점에 `--preload-file`/`--shell-file`로 바이너리에 구워진다.
`.cpp`를 건드리지 않고 셰이더/HTML만 수정하면 NMake가 MiniEngine 타겟을 "최신"으로
판단해 **재링크를 스킵** → `MiniEngine.data` 미재생성 → 옛 셰이더가 그대로 실행.
`cmake --build` 출력이 `[100%] Built target MiniEngine`만 찍고 컴파일 라인이 전혀
없는 것으로 확인됨.

**수정:** `CMakeLists.txt`에 WGSL 셰이더 + 셸 HTML을 `LINK_DEPENDS`로 등록 →
이들이 바뀌면 강제 재링크 + preload 재패키지.

### 3.2 `EmscriptenToolchain.cmake`의 `WIN32` 오판

`cmake/EmscriptenToolchain.cmake`가 Windows 호스트에서 `emar.bat`/`emranlib.bat`를
쓰도록 분기할 때 `if(WIN32 ...)`를 사용했다. 그러나 이 toolchain이
`CMAKE_SYSTEM_NAME=Emscripten`(크로스컴파일)을 설정하므로 `WIN32`는 **타깃
(Emscripten) = 거짓**이다. 따라서 else 분기로 빠져 `CMAKE_AR=emar`(확장자 없는
파이썬 래퍼)가 됐고, Windows가 이를 실행하지 못해 **fresh CMake configure의 컴파일러
테스트가 링크 단계에서 실패**했다. 기존에 작동하던 `build_wasm/CMakeCache.txt`가
잠복시켜, `clean` 후 재설정하려는 순간에야 드러났다.

**수정:** `WIN32` → `CMAKE_HOST_WIN32` (호스트 기준 판정).

### 3.3 `wasm.ps1`의 emcmake PATH 누락

`Invoke-Emsdk`가 `call emsdk_env.bat`만으로 cmd 세션에 emscripten 디렉터리를 PATH에
올리지 못해(POSIX식 export를 출력하는 경우 존재), fresh `emcmake` configure가
`'emcmake' is not recognized`로 실패했다.

**수정:** `Invoke-Emsdk`가 `$EmsdkDir\upstream\emscripten`을 PATH에 명시적으로
선행 추가.

### 3.4 브라우저 캐시 — `python -m http.server`

stock `http.server`는 `Cache-Control`을 보내지 않아 브라우저가 큰
`MiniEngine.wasm`/`.data`를 휴리스틱 캐싱한다. Emscripten 로더는 이들을 `fetch()`로
받으므로 페이지 하드 리프레시로도 갱신되지 않을 수 있어, 새 빌드가 옛 바이너리로
가려졌다. (※ 사용자 환경에서는 서버를 매번 끄고 빌드해 이 요인 단독 영향은
제한적이었으나, 재발 방지를 위해 영구 수정.)

**수정:** `scripts/serve_nocache.py` 추가(`Cache-Control: no-store` + `Last-Modified`
제거). `wasm.ps1 serve`가 이 서버를 사용.

---

## 4. 진단 여정 (시간순) — 무엇을 시도하고 무엇을 배제했나

순서대로의 가설·수정과, 각 단계가 좁혀준 범위. 대부분은 그 자체로 타당했으나
2장의 구조체 버그(+3장 환경 결함)에 가려 단독으로는 해결되지 않았다.

1. **WebGPU 좌표 관례 (deferred_lighting.wgsl)**
   - shadow UV: `projCoords.y = projCoords.y*0.5+0.5` → `-projCoords.y*0.5+0.5`
     (WebGPU는 NDC Y=+1이 텍스처 V=0)
   - worldPos 재구성: `ndc.y = uv.y*2-1` → `1 - uv.y*2` (WebGPU 프레임버퍼 Y=0이
     화면 상단). — 타당한 수정이며 유지. 단독으론 증상 불변.

2. **ASYNCIFY 충돌** — `Cannot have multiple async operations in flight`. JS
   이벤트/`setInterval`이 WASM suspend 중 바인딩 호출. → HTML에서 pending 값 큐 +
   `requestAnimationFrame` 적용으로 해소.

3. **cascade frustum 정밀도/카메라 의존** — `Camera` far=50000으로 인한
   언프로젝션 정밀도 붕괴, cascade가 카메라 frustum에 매 프레임 재피팅돼 출렁임.
   → bounded-far 언프로젝션, scene-radius 클램프, bounding sphere + 텍셀 스냅핑.
   증상 완화됐으나 근본 미해결.

4. **firstInstance / instance_index 차이** — 지면(object 0)을 셰도우에서
   제외하려 `firstInstance`/`objects[idx+1]` 트릭 사용. Vulkan은 firstInstance를
   `gl_InstanceIndex`에 합산, WebGPU/Dawn 거동이 달라 신뢰 불가로 판명. → 전
   인스턴스 직접 인덱싱 + 셰이더 내 **기하학적 지면 컬링**(거대 AABB 클립아웃)
   으로 대체.

5. **CSM 전면 폐기 → 단일 씬-고정 맵 + PCSS** — 작은 고정 쇼케이스 씬에 4-cascade
   CSM은 과한 복잡도. 카메라 무관 단일 행렬(매 프레임 상수 → 출렁임 구조적
   불가)로 교체, UBO 레이아웃 유지(4슬롯에 동일 행렬 복제 → C++/Vulkan/바인딩
   변경 0). 소프트 섀도우는 PCSS(blocker search → penumbra → 가변 Poisson PCF).

6. **태양각** — 기본 `sunDirection=(0.7,0.25,0.5)`(고도 ~16°, 일몰)은 물리적으로
   맞지만 그림자가 건물 높이의 3–4배로 과도. → `(0.35,0.88,0.32)`(고도 ~62°),
   sunColor도 주광색으로.

7. **단계별 디버그 시각화(결정타)** — deferred에 임시 디버그 뷰 추가:
   - "Shadow" = 순수 그림자항 → frustum 박스 전체가 검정 = 그림자 계산 자체 문제
     (조명 아님) 확정
   - "ShadowMap" = 프래그먼트 light-space UV의 원시 shadow map 깊이 → **전면에
     부드러운 깊이 그라데이션** 관측. 비었거나(0) 깨끗하지(1) 않고 "거대 평면
     occluder가 들어찬" 형태 → 셰도우 패스가 지면을, 또는 어긋난 데이터를
     렌더하고 있음을 강하게 시사. 이 관측이 2장(구조체 스트라이드 불일치) 확정의
     결정적 단서가 됐다.

> 교훈: 빌드 환경이 코드 수정을 가릴 수 있을 때, "산출물 타임스탬프·바이너리
> 내부 문자열 검증"과 "중간 단계 시각화"를 진단 루프에 반드시 포함해야 한다.
> 셰이더 단독 추론만으로는 가려진 환경 결함을 발견할 수 없었다.

---

## 5. 최종 해결 구성 (현재 코드 상태)

### 5.1 `shaders/shadow.wgsl`
- `ObjectData` 128B로 수정(`texParams` 제거), C++/타 셰이더와 일치 명시 주석.
- 전 인스턴스 직접 인덱싱(`objects[input.instanceIndex]`).
- 거대 AABB(가로폭 > 10000) 객체(지면)를 NDC z=-2로 클립아웃 — 백엔드
  instance_index 거동에 무의존한 기하학적 지면 컬링.

### 5.2 `src/rendering/ShadowRenderer.cpp`
- 4-cascade CSM 폐기. `computeCascadeMatrix`는 카메라 무관 **단일 씬-고정 행렬**:
  씬 바운딩 스피어(원점 중심, `sceneRadius` 기반, 태양 고도로 그림자 throw 보정)에
  ortho를 맞추고 태양 방향에서 바라봄. WebGPU/Vulkan 깊이 [0,1] 리맵 + Y-flip은
  shadow.wgsl(기록)·deferred(읽기)와 일관.
- `updateLightMatrices`는 그 행렬을 4 UBO 슬롯에 복제, `cascadeSplits`는 큰 값으로
  설정(셰이더의 cascade 선택이 항상 슬롯 0으로 귀결, 전부 동일).

### 5.3 `shaders/deferred_lighting.wgsl`
- `calculateCSMShadow`를 단일 맵 + **PCSS**로 재작성:
  16-탭 Poisson blocker search → 닮은꼴 penumbra 추정 → penumbra 비례 가변 반경
  Poisson PCF. 임시 디버그 뷰(9/13)는 제거, 가드 원복.

### 5.4 `src/rendering/Renderer.cpp`
- 셰도우 draw를 전 인스턴스 `drawIndexed(meshIndexCount, instanceCount, 0,0,0)`로
  단순화(지면 컬링은 셰이더가 담당).

### 5.5 `src/ui/ImGuiManager.hpp`
- 기본 `sunDirection`/`sunColor`를 자연스러운 주간(고도 ~62°)으로.

---

## 6. 빌드 환경 수정 (재발 방지)

| 파일 | 변경 |
| --- | --- |
| `CMakeLists.txt` | WGSL/셸 HTML을 `LINK_DEPENDS`로 등록 → 셰이더/HTML 변경 시 강제 재링크·재패키지 |
| `cmake/EmscriptenToolchain.cmake` | `WIN32` → `CMAKE_HOST_WIN32` (크로스컴파일 시 호스트 기준 ar 선택) |
| `scripts/wasm.ps1` | `Invoke-Emsdk`가 emscripten 디렉터리를 PATH에 명시 선행 추가; `serve`가 no-cache 서버 사용 |
| `scripts/serve_nocache.py` | 신규 — `Cache-Control: no-store` + `Last-Modified` 제거 dev 서버 |

---

## 7. 교훈

- **멀티 셰이더 공유 SSBO 구조체는 단일 진실원천이 필요하다.** `ObjectData`가
  3개 셰이더 + C++에 각각 손으로 정의돼 있었고, 그중 하나만 어긋나 전체 셰도우가
  무너졌다. 공유 구조체는 한 곳에서 생성/포함하거나 최소한 스트라이드 동등성을
  강제하는 검증이 필요하다.
- **빌드 환경이 코드 수정을 조용히 가릴 수 있다.** 증분 미감지·toolchain 분기
  오판·캐시. 진단 시 "내 수정이 실제 바이너리에 들어갔는가"를 산출물 타임스탬프·
  바이너리 문자열로 먼저 검증해야 추론이 의미를 가진다.
- **작은 고정 씬에 CSM은 과설계.** 카메라 무관 단일 씬-고정 맵이 더 단순하고
  안정적이며, 버그 표면적이 훨씬 작다.
