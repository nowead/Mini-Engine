# 변경 이력 — 2026-05-26

> 작업 범위: ENGINE_ROADMAP §C — **TAA (Temporal Anti-Aliasing)**, Vulkan
> 네이티브 우선. 모션 벡터 G-Buffer(C0a) + 서브픽셀 지터(C1)는 직전 커밋
> `53b618e`에 기반으로 들어갔고, 이 문서는 그 위에 **리졸브 패스(C2)**를
> 얹어 TAA를 완성하기까지의 설계와 디버깅 여정을 다룬다.

---

## 1. 구성 — 3단계

| 단계 | 내용 | 상태 |
| --- | --- | --- |
| C0a | 모션 벡터 G-Buffer (4th RG16Float target, prevViewProj, vert/frag velocity) | ✅ `53b618e` |
| C1  | Halton(2,3) 서브픽셀 지터 (게이트 off) | ✅ `53b618e` |
| C2  | 리졸브 패스 (리프로젝션 + variance clipping + 블렌드) + 히스토리 + UI 토글 | ✅ 이 커밋 |

모두 **Vulkan 전용** — WebGPU는 G-Buffer가 3-target이라 포팅은 후속.

---

## 2. 파이프라인 통합 — persistent 히스토리를 RenderGraph에

가장 큰 난제는 TAA 히스토리(이전 프레임 리졸브 결과)를 RenderGraph에 어떻게
넣느냐였다. `rgHDR`/`rgGBuffer*` 같은 기존 리소스는 **프레임마다 재import되는
transient** 자원이지만, 히스토리는 **프레임 간 내용이 보존돼야 하는
persistent** 자원이다.

RenderGraph 상태 모델을 읽고 확인한 사실:

- `importTexture(name, tex, initialState)`는 `initialState.layout`을 그 자원의
  현재 레이아웃으로 신뢰한다. `transitionTex`가 `currentState.layout →
  needed.layout`으로 배리어를 emit하므로, **initialState.layout이 Undefined가
  아니면 내용이 보존**된다(Undefined면 discard).
- 그래프는 매 프레임 `reset()` → 자원 상태가 초기화된다. 따라서 persistent
  히스토리는 **직전 프레임이 남긴 레이아웃을 내가 멤버로 추적**해서 다음 프레임
  import의 initialState로 넘겨야 내용이 보존된다.

### 채택한 설계

- **핑퐁 히스토리 2개** (RGBA16Float, Storage|Sampled|CopySrc), HDR 타깃과 같이
  생성/resize.
- 매 프레임: `history[read]`/`history[write]`를 **추적 레이아웃**(`m_taaHistoryState[]`)
  으로 import → 컴퓨트 리졸브 패스가 `hdrColor`(현재) + velocity + `history[read]`
  를 읽어 `history[write]`에 기록 → **copy 패스**가 `history[write]`를 `hdrColor`로
  복사 → bloom/postprocess는 `hdrColor`를 그대로 읽음(다운스트림 재라우팅 회피).
- 실행 후 각 히스토리가 남은 레이아웃을 결정론적으로 기록(`read`는 sampled →
  ShaderReadOnly, `write`는 storage 후 copy-read → TransferSrc)하고 핑퐁 스왑.
- 첫 프레임은 `historyValid=0` → 현재 색 그대로 출력(히스토리 시드).

### RenderGraph 확장 — CopySrc/CopyDst

copy(history→hdrColor)에 transfer 배리어가 필요했는데 `RGAccess`에 copy 계열이
없었다. `RGAccess::CopySrc/CopyDst` + `inferTexState`(eCopy / TransferRead·Write /
TransferSrc·DstOptimal) + `isTexAccess`를 추가 — 작고 재사용 가능한 확장. 이제
copy도 그래프 패스로 배리어가 자동 추론된다.

---

## 3. 디버깅 여정 — "살짝 뿌옇다"

C2 구현 후 첫 실행: 렌더링 결함(고스팅/크래시/검증 에러)은 없었고 건물·헬멧
계단현상도 사라졌으나, 화면이 **살짝 뿌옇게** 보였다. 정적 카메라에서도.

### 원인 — 지터된 행렬로 계산한 velocity

초기 구현은 velocity를 **지터된** proj로 계산했다(curr/prev 모두 지터 포함).
의도는 "양쪽 지터가 자기상쇄돼 리프로젝션이 일관된다"였다. 그러나 결과는:

- 정적 카메라에서도 velocity = currUV − prevUV = **지터 차이**(매 프레임 다른
  서브픽셀)라 0이 아니다.
- 그래서 매 프레임 히스토리를 **서브픽셀 어긋난 위치에서 bilinear 재샘플** →
  재샘플 블러가 history에 누적 → 화면이 점점 뿌예진다.

### 수정 — velocity는 지터 없는 행렬로

올바른 TAA는 **모션 벡터를 지터-free로** 둔다. 지터는 현재 프레임의 래스터화
(gl_Position)에만 적용해 서브픽셀 슈퍼샘플링을 제공하고, velocity는 순수 기하
모션만 담아야 한다. 그러면:

- 정적 카메라 → velocity = 0 → 히스토리를 **정확히 같은 텍셀**에서 샘플(bilinear
  보간 없음) → 블러 누적 없음. 매 프레임 다르게 지터된 현재 색이 history에
  누적돼 **선명한 슈퍼샘플 결과로 수렴**(= AA + 선명).

구현: UBO에 `currViewProjNoJitter` 추가, `prevViewProj`도 지터 없는 값으로 저장.
`gl_Position`은 지터된 `ubo.proj`를, `fragCurrClip/fragPrevClip`(velocity)은
지터 없는 `currViewProjNoJitter`/`prevViewProj`를 사용. (둘 다 UBO 맨 끝에 추가해
다른 셰이더 오프셋 불변.)

결과: 정적 카메라에서 가장자리가 선명하게 수렴, 뿌연 느낌 해소. 사용자 확인
완료(건물/헬멧 계단현상 없음).

---

## 4. 알고리즘 (taa_resolve.comp.glsl)

1. 현재 색 `curr` 샘플. `historyValid==0`이면 그대로 출력(시드).
2. 현재 프레임의 **3×3 neighborhood min/max** 계산(variance clipping용).
3. `histUV = uv − velocity`로 히스토리 리프로젝션. 화면 밖이면(디스오클루전)
   현재 색 사용.
4. 히스토리를 neighborhood로 `clamp`(고스팅/디스오클루전 억제) 후
   `mix(curr, clampedHist, 0.9)` 블렌드.

---

## 5. 수정 파일 요약

| 파일 | 변경 |
| --- | --- |
| `src/rendering/graph/RenderGraphResource.hpp` | `RGAccess::CopySrc/CopyDst` |
| `src/rendering/graph/RenderGraph.cpp` | `inferTexState`/`isTexAccess`에 copy 케이스 |
| `shaders/taa_resolve.comp.glsl` | **신규** — 리프로젝션 + variance clipping + 블렌드 |
| `CMakeLists.txt` | taa_resolve.comp.spv 컴파일 + SPV 리스트 |
| `src/rendering/Renderer.{hpp,cpp}` | 핑퐁 히스토리(createHDRRenderTarget), `createTAAResources`(파이프라인 + 핑퐁 bind group), drawFrame RenderGraph 통합(resolve + copy + 상태 추적), 지터(updateRHIUniformBuffer), velocity 지터-free 수정, hdrColor += CopyDst |
| `src/utils/Vertex.hpp` | UBO 끝에 `prevViewProj`(지터 없음) + `currViewProjNoJitter` |
| `shaders/gbuffer.vert.glsl` | 전체 UBO 선언 + 지터 없는 curr/prev로 velocity 출력 |
| `src/ui/ImGuiManager.{hpp,cpp}` + `src/Application.cpp` | TAA on/off 토글 |

---

## 6. 알려진 한계 / 후속

- **그림자 그리드 패턴**: 지면에 그림자 맵 텍셀 그리드(PCF/해상도 에일리어싱)가
  보인다. TAA와 무관(그림자 맵 품질). 후속으로 PCF 개선/해상도/슬로프 바이어스
  검토.
- **WebGPU 포팅 미완**: G-Buffer가 3-target(velocity 없음)이라 WebGPU는 아직
  FXAA만. 4-target + WGSL 리졸브 포팅 후속.
- **per-object 모션 벡터 없음**: 현재 카메라 모션만. 애니메이션 오브젝트는
  per-object 이전 world 행렬 필요(빌딩 높이 변화 등에서 고스팅 가능).
- **샤픈 패스 없음**: 이동 중 약간의 소프트닝은 정상. 필요 시 언샤픈/Catmull-Rom
  히스토리 샘플 후속.

AB 작업 단위(sub-task 1–9 + Vulkan parity + 8)에 이어 C(TAA)까지 종결. 다음은
로드맵 D(멀티스레드 커맨드 레코딩) 또는 그림자 품질 개선.

---

## 변경 이력 (2부) — 그림자 품질 (그리드/계단 제거)

> §6에서 "후속"으로 미뤘던 지면 그림자의 그리드/계단 패턴을 바로 이어서
> 해결. 두 개의 독립적 원인이 겹쳐 있었다.

### 7. 증상

TAA로 건물/헬멧 계단현상은 사라졌으나, 지면에 드리운 그림자가 굵은 그리드/
계단 패턴으로 보였다(품질 저하).

### 8. 원인 둘

1. **과도한 ortho — 해상도 90%+ 낭비**: `shadowSceneRadius` 기본값이 200인데
   기본 씬(4×4 그리드)의 실제 클러스터 반경은 ~64다. 게다가
   `computeCascadeMatrix`가 저각도 태양의 그림자 투사를 담으려 ortho를
   `R = Rc + Rc·clamp(stretch≈3.4, 0, 6)` ≈ **880**까지 팽창시킨다. 2048² 맵이
   1760×1760 영역을 덮어 텍셀당 ~0.86유닛 → 클러스터(~100유닛)에 텍셀 ~100개뿐.
   그림자 맵 해상도의 대부분이 빈 공간에 낭비됐다.
2. **Nearest PCF**: 그림자 샘플러가 Nearest라, 3×3 PCF 각 탭이 텍셀 중심에
   스냅 → 큰 텍셀 위에서 거친 계단.

### 9. 수정 둘

1. **ortho 축소**: `shadowSceneRadius` 기본값 200 → **80**(기본 클러스터 ~64에
   헤드룸). `Application::regenerateBuildings`의 floor도 200 → 80(작은 스트레스
   그리드도 타이트하게; 큰 그리드는 `gridExtent·0.6`으로 스케일). R ≈ 880 → 352,
   텍셀 ~2.5× 조밀.
2. **하드웨어 PCF 비교 샘플러** (Vulkan): 그림자 샘플러를 `compareEnable=true`,
   `compareOp=LessOrEqual`, Linear 필터로 변경. `deferred_lighting.frag.glsl`의
   PCF 루프를 `sampler2DArrayShadow` + `texture(..., vec4(uv, layer, ref))`로
   바꿔 각 탭이 하드웨어 2×2 bilinear 깊이 비교 → 3×3 루프와 합쳐 ~6×6 필터링.
   `lit` 비율을 누적해 `shadow = (1 − lit)·shadowStrength`.

   WebGPU는 그대로 — 샘플러 생성을 `#ifndef __EMSCRIPTEN__`로 가드(Vulkan만
   비교 샘플러), WGSL deferred는 수동 비교 유지(`sampler_comparison` 미배선).
   RHI는 이미 `SamplerDesc.compareEnable/compareOp`를 VkSampler에 반영함.

ortho 축소 + 하드웨어 PCF 두 가지로 그리드/계단이 거의 사라짐. 사용자 확인 완료.

### 10. 수정 파일 요약 (2부)

| 파일 | 변경 |
| --- | --- |
| `src/rendering/Renderer.hpp` | `shadowSceneRadius` 기본 200 → 80 + 사유 주석 |
| `src/Application.cpp` | `regenerateBuildings` 그림자 반경 floor 200 → 80 |
| `src/rendering/ShadowRenderer.cpp` | 그림자 샘플러 Vulkan 비교 샘플러(Linear, LessOrEqual), WebGPU는 Nearest 유지 |
| `shaders/deferred_lighting.frag.glsl` | `sampler2DArrayShadow` 하드웨어 PCF (lit 비율 누적) |

남은 그림자 후속: 더 넓은 커널/Poisson, 그림자 맵 4096, 4개 동일 cascade
중복 제거(현재 단일 scene-fit 행렬을 4 레이어에 복제 → 3 레이어 낭비).
