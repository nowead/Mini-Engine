# 변경 이력 — 2026-05-24

> 작업 범위: ENGINE_ROADMAP §4.4 sub-task 9 (시연 통합) — A/B 분할 비교에
> "머티리얼 텍스처 on/off" 모드 추가. 헬멧 한 개를 화면 좌/우로 나눠
> 좌측은 step 6 이전의 plain 셰이딩, 우측은 풀 PBR로 동시에 보여준다.
> AB 작업 단위의 시연 측면을 봉인하는 작은 마무리 단계.

---

## 1. 개요

step 6에서 완성한 PBR 머티리얼(baseColor / normal / MR / AO / emissive)이
"있을 때 vs 없을 때"를 한 화면에서 직접 대비시키는 게 목표. 이미 2026-05-20에
만든 A/B 분할 인프라(`abSplitX` float, 0 = off / (0,1) = 분할 위치)가 있었고,
그때는 두 레이어에서만 작동했다:

- **deferred lighting**: 좌측에서 점광원 skip
- **postprocess**: 좌측에서 SSAO=0, 흰 분할선 + "Baseline"/"Full pipeline" 라벨

이번 작업은 여기에 **gbuffer 레이어**를 추가한다 — 좌측에서 PBR 텍스처를 전부
무시하고 ObjectData 스칼라 factor로 fallback(roadmap 표현으로 "sentinel
강제"). 그 결과 헬멧이 분할선 기준으로 좌측 = 흰색 plain(텍스처·노멀맵·금속·
emissive 전부 없음), 우측 = 완성된 PBR로 갈린다. 머티리얼 작업의 효과가
한 스크린샷에 박힘.

---

## 2. 핵심 난제 — gbuffer fragment가 abSplitX를 모른다

A/B 분할은 **screen-space x 좌표**로 판정한다. deferred lighting과 postprocess
셰이더는 `UniformBufferObject`(abSplitX가 마지막 필드)를 전부 선언해서 접근할
수 있었다. 그러나 `gbuffer.wgsl`의 UBO 선언은 model/view/proj 세 개만 노출한다:

```wgsl
struct GBufferUBO {
    model: mat4x4<f32>,  // offset   0
    view:  mat4x4<f32>,  // offset  64
    proj:  mat4x4<f32>,  // offset 128
}
```

abSplitX는 같은 버퍼의 offset 4764(전체 UBO는 ~4768B, PointLight 배열 128개
포함)에 있어서, gbuffer가 그걸 읽으려면 그 사이 모든 필드(특히 4KB짜리
PointLight 배열)를 선언해야 한다. gbuffer는 의도적으로 가볍게 유지하는
셰이더라 이건 과하다.

### 검토한 선택지

1. **전체 UBO struct 재선언** (building.wgsl처럼) — verbose, gbuffer를 무겁게.
2. **C++ UBO에서 abSplitX를 proj 뒤로 재배치** — UBO를 선언하는 모든 셰이더
   (building/deferred/shadow × glsl/wgsl)를 동시 수정해야 함. 침습 큼.
3. **ObjectData에 abSplitX 복제** — frame-wide 값을 per-instance에 100번 중복.
   낭비 + 8개 셰이더 ObjectData 동기.
4. **deferred lighting에서 처리** — abSplitX는 거기 있지만, gbuffer가 이미
   텍스처를 샘플해서 G-Buffer에 기록한 뒤라 "텍스처를 되돌릴" 수 없음. 노멀 맵
   효과는 world-space normal에서 역산 불가.
5. **material bind group(set 2)에 작은 frame-state UBO 추가** ← 채택.

### 채택: set 2 binding 6에 frame-state UBO

material bind group은 WebGPU 전용(Vulkan은 set 2를 bindless가 점유). A/B
시연도 브라우저 전용이라 blast radius가 정확히 일치. 16바이트 UBO 하나를
새로 만들어 abSplitX + 화면 크기를 싣는다. bind group은 init 시 핸들로
참조하고, 내용만 매 프레임 갱신하므로 bind group 재생성 불필요.

---

## 3. 구현

### 3.1 C++ 측

- `Renderer::materialFrameUBO` (16B) 신설.
- `createMaterialBindGroupInfrastructure`: layout에 binding 6 = uniform buffer
  추가, UBO 생성, default bind group에 entry 추가.
- `uploadShowcaseMaterialTextures`: showcase bind group에도 binding 6 추가.
- `drawFrame`: gbuffer pass 직전에 `{abSplitX, screenW, screenH, pad}` 작성.
  전부 `#ifdef __EMSCRIPTEN__` 가드 — 네이티브 무영향.

### 3.2 WGSL 측 — vec2 정렬 함정

처음엔 frame-state struct를 이렇게 선언했다:

```wgsl
struct FrameState {
    abSplitX:   f32,        // offset 0
    screenSize: vec2<f32>,  // ← 의도: offset 4
    _pad:       f32,
}
```

그러나 **WGSL `vec2<f32>`는 8바이트 정렬**이라 `screenSize`가 offset 8로
밀린다. C++은 `{float abSplitX; float screenW; float screenH; float pad;}`로
abSplitX@0, screenW@4로 tight packing — **desync**. 화면 크기를 엉뚱한
오프셋에서 읽어 분할 위치가 깨진다.

→ all-scalar 레이아웃으로 교정:

```wgsl
struct FrameState {
    abSplitX: f32,  // 0
    screenW:  f32,  // 4
    screenH:  f32,  // 8
    _pad:     f32,  // 12
}
```

모든 멤버가 f32(align 4)라 C++과 byte-for-byte 일치. uniform buffer는 16B
배수여야 하는데 4 floats = 16B로 충족.

(이건 RHI UBO 작업에서 반복되는 함정 — 2026-05-20 PostProcessParams 확장 때도
같은 std140/WGSL 정렬을 신경 썼다. 셰이더 UBO에 vec2/vec3를 넣을 땐 항상
오프셋을 손으로 확인할 것.)

### 3.3 fragment 분기 — uniform control flow 유지

```wgsl
let abActive       = frameState.abSplitX > 0.0;
let uvScreenX      = input.position.x / frameState.screenW;
let onBaselineSide = abActive && uvScreenX < frameState.abSplitX;

// 모든 텍스처는 항상 sample (uniform control flow), select로 결과만 선택
let N         = select(Ntextured,         Nvert,           onBaselineSide);
let albedo    = select(albedoTextured,    input.albedo,    onBaselineSide);
let roughness = select(roughnessTextured, input.roughness, onBaselineSide);
let metallic  = select(metallicTextured,  input.metallic,  onBaselineSide);
let ao        = select(aoTextured,        input.ao,        onBaselineSide);
let emissive  = select(emissiveTextured,  vec3<f32>(0.0),  onBaselineSide);
```

`textureSample`을 분기 안에 넣지 않고 **항상 실행한 뒤 `select`로 고른다**.
WebGPU는 텍스처 샘플에 uniform control flow를 요구하므로(미분 계산 때문),
`if (onBaselineSide) { ... } else { textureSample ... }`로 감싸면 non-uniform
control flow 경고/에러 위험. 항상 샘플 + select가 정석. baseline 쪽은
sample 결과를 버릴 뿐이라 약간의 낭비지만 정확하고 안전.

baseline 측 값은 step 6 이전과 정확히 동일: factor-only albedo(헬멧은
baseColorFactor가 흰색이라 흰 헬멧), vertex normal, 텍스처 없는 roughness/
metallic/ao, emissive 0.

---

## 4. 결과

A/B Compare 토글 ON + 슬라이더 조정 시 헬멧이 분할선 기준으로:

- **좌측 (Baseline)**: 흰색 plain 헬멧 — 텍스처/노멀맵/금속/AO/emissive 전부
  없는, step 6 이전의 매끈한 모습. 점광원도 없음(deferred), SSAO도 없음
  (postprocess).
- **우측 (Full pipeline)**: 완성된 PBR — albedo 패널·노멀맵 디테일·금속 영역·
  visor emissive.

세 레이어(gbuffer 머티리얼 / deferred 점광원 / postprocess SSAO)가 동일한
`abSplitX` 한 값으로 일관 작동. 흰 분할선과 "Baseline"/"Full pipeline" 라벨도
함께 표시.

AB 작업 단위의 시연 측면 종결. 다음은 Vulkan parity (네이티브 빌드도 헬멧을
풀 PBR로).

---

## 5. 수정 파일 요약

| 파일 | 변경 |
| --- | --- |
| `src/rendering/Renderer.hpp` | `materialFrameUBO` 멤버 (16B, WebGPU only) |
| `src/rendering/Renderer.cpp` | material layout binding 6 = uniform buffer, UBO 생성, default + showcase bind group entry, drawFrame에서 매 프레임 `{abSplitX, W, H, pad}` 작성 (전부 `#ifdef __EMSCRIPTEN__`) |
| `shaders/gbuffer.wgsl` | `FrameState` UBO(all-scalar, set 2 binding 6) + fragment에서 onBaselineSide 판정 + textured/factor `select` 분기 |
| `docs/current/engine-roadmap/ENGINE_ROADMAP.md` | §0 현재 위치 + §4.4 sub-task 9 ✅ |

C++ 변경은 전부 Emscripten 가드 안 — 네이티브 빌드 무영향. WGSL이 본 작업의
핵심.

---

## 6. 후속

| 항목 | 상태 |
| --- | --- |
| AB sub-task 1–7 (PBR sampling) | ✅ |
| AB sub-task 9 (시연 통합) | ✅ (이 문서) |
| Vulkan parity | ⬜ **다음** — set 2 × bindless 충돌 해소 |
| AB sub-task 8 (SceneNode 최소 구현) | ⬜ |
| C (TAA) | ⬜ AB 완전 종결 후 |

알려진 한계는 이전과 동일 (`CHANGELOG_2026-05-22.md` §7): Vulkan parity 부재,
tangent.w 미저장, emissive HDR 미지원, ORM-packed MR fallback 부재.

---

## 변경 이력 (2부) — Vulkan PBR parity + 네이티브 resize 크래시

> 작업 범위: 같은 날 이어진 별개 작업 단위. 위 1부(A/B 시연 통합)는 WebGPU
> 전용이었고, 이번엔 **네이티브 Vulkan**에서도 헬멧을 풀 PBR로 렌더링한다
> (ENGINE_ROADMAP §3 "Vulkan parity"). 작업 도중 드러난 두 개의 잠복 버그
> — bindless가 실제로는 꺼져 있던 초기화 순서 문제, 그리고 네이티브 deferred
> 경로의 resize 크래시 — 의 디버깅 여정도 함께 기록한다.

---

## 7. 목표와 슬롯 설계

WebGPU는 set 2 머티리얼 bind group(baseColor/normal/MR/emissive/AO 분리
바인딩)으로 풀 PBR을 한다. Vulkan에는 set 2를 bindless 텍스처 배열
(`allTextures[]`)이 점유하므로 같은 인프라를 재사용한다: 헬멧의 5개 텍스처를
bindless에 등록하고 인덱스를 `ObjectData`에 실어 G-Buffer 셰이더가
`allTextures[index]`를 샘플.

### 슬롯 할당 — 빌딩 회귀 0을 위한 선택

핵심 제약: `ObjectData`는 144B 고정이고 `textureIndices`는 uvec4(4칸)뿐인데
헬멧은 5개 맵(baseColor/normal/MR/emissive/AO)이 필요. 그리고 빌딩은 이미
albedo bindless 인덱스를 `roughnessAOPad.b`에 쓰고 있다(`textureIndices`는
sentinel로 방치). 조사해보니 **`textureIndices`는 현재 어떤 활성 셰이더
경로에서도 안 쓰인다** (WebGPU는 bind group으로 바인딩, 기존 Vulkan vert는
`roughnessAOPad.b`만 읽음). 그래서 4칸이 통째로 비어 있었다.

채택한 매핑:

| 슬롯 | 용도 | 비고 |
| --- | --- | --- |
| `roughnessAOPad.b` | baseColor 인덱스 | 빌딩과 공유 — 기존 그대로 |
| `textureIndices.x` | normal | 빌딩은 sentinel |
| `textureIndices.y` | metallicRoughness (G=rough, B=metal) | 〃 |
| `textureIndices.z` | emissive | 〃 |
| `textureIndices.w` | occlusion (AO) | 〃 |

빌딩은 `textureIndices`를 sentinel로 두므로 셰이더가 normal/MR/AO/emissive
샘플을 건너뛰고 **이전과 동일하게** 동작. albedo 경로(factor 곱 없이
`texture(...).rgb`)도 손대지 않아 빌딩 색 보존. 글로벌 `static_assert(144)`
스트라이드 불변.

색공간은 텍스처 뷰 포맷이 처리: baseColor/emissive는 sRGB 뷰라 HW가
linear로 디코드, normal/MR/AO는 linear 뷰. 셰이더는 추가 `pow()` 없음.

### 등록 시퀀싱

`setShowcaseMesh`는 텍스처 등록 **이전에** ObjectData를 쓴다(이 시점엔 모든
인덱스가 sentinel). 그래서 ShowcaseAsset에 CPU측 `ObjectData` 사본을 두고,
`uploadShowcaseMaterialTextures`가 텍스처 등록 후 인덱스를 패치해
`objectBuffer`를 재업로드. 헬멧 2K 맵용으로 Linear 샘플러를 따로 생성
(`bindlessSampler`는 1×1 빌딩 솔리드용 Nearest라 헬멧엔 에일리어싱).

---

## 8. 디버깅 여정 — "헬멧이 회색" → "화면 전체가 파란색"

### 증상 1: 네이티브에서 헬멧이 회색

WebGPU에선 풀 PBR인데 네이티브는 헬멧이 무채색. 첫 가설은 "showcase
텍스처가 bindless에 등록 안 됨" — 부분적으로 맞았다(Vulkan 등록 경로가
아예 없었다). 위 §7 구현으로 등록을 추가하고 로그로 확인:

```text
[INFO][Renderer] Showcase bindless slots: baseColor=3 normal=4 mr=5 emissive=6 ao=7
[INFO][Renderer] Showcase material slot resolution: ... 전부 real
```

등록은 완벽했다. 그런데 빌드 후 —

### 증상 2: 화면 전체가 파란색 (건물·지면·헬멧 전부)

헬멧만이 아니라 **모든 지오메트리**가 파랗게. 모든 지오메트리에 공통인 건
deferred lighting의 새 한 줄 `color += gb2.gba`(emissive)뿐. `gBuffer2`를
`vec4(ao, emissive)`로 재정의했으니, gba에 (0,0,1) 같은 값이 들어가면 전
화면이 파래진다.

### 틀린 가설들

- "내 bindless gbuffer.frag가 emissive를 잘못 쓴다?" → 아니다. 그 셰이더는
  빌딩에 emissive=0을 써서 gba=(0,0,0). 빌딩이 파랗다는 건 이 셰이더가
  **안 돌고 있다**는 뜻.
- "샘플러/포맷 문제?" → 색이 정확히 파랑((0,0,1))이라 데이터가 아니라
  **상수**가 의심됨.

### 실제 원인 — bindless가 애초에 꺼져 있었다

로그를 다시 읽으니 결정적 두 줄의 **순서**가 보였다:

```text
[GBufferPass] Initialized 1280x720 (bindless disabled)   ← 먼저
[BindlessTextureManager] Ready — 4096 slots              ← 나중
```

`Renderer` 초기화에서 `createGBufferPass()`가 `createBindlessResources()`
**보다 먼저** 호출됐다. GBufferPass는 init 시점에 bindless 디스크립터셋
레이아웃이 있는지로 bindless vs nobindless 프래그먼트 셰이더를 고르는데,
그 시점엔 `bindlessTextureManager`가 아직 null → **nobindless 셰이더 선택**.

`gbuffer_nobindless.frag.glsl`은 (a) 인덱스를 전부 무시하고 procedural
색만 쓰고 (b) `gBuffer2 = vec4(ao, 0, 0, 1.0)`를 쓴다 — 옛날엔 .gba가
패딩이라 alpha=1이 무해했다. 그런데 deferred에서 `gb2.gba`를 emissive로
읽게 바꾸니 **(0,0,1) = 파랑**이 전 화면에 더해진 것.

즉 디바이스는 bindless를 지원("Descriptor indexing supported")하는데도
**초기화 순서 때문에 네이티브는 줄곧 nobindless 폴백으로 렌더링**하고
있었다. 이게 "헬멧이 회색"의 진짜 원인이기도 했다 — 텍스처를 등록해도
샘플하는 셰이더가 아니었으니까.

### 수정 — 초기화 순서 + 폴백 방어

**수정 1 — 순서 교정**: `createBindlessResources()`를 `createGBufferPass()`
**이전으로** 이동(Emscripten은 그대로 `createMaterialBindGroupInfrastructure`).
이제 GBufferPass가 bindless 셰이더를 고른다:

```text
[INFO][Renderer] Bindless resources ready (1)
[GBufferPass] Initialized 1280x720 (bindless enabled)   ← 고쳐짐
```

**수정 2 — 방어**: `gbuffer_nobindless.frag.glsl`의 `gBuffer2`를
`vec4(ao, 0, 0, 0)`로. 진짜로 bindless 미지원인 디바이스(lavapipe 등)
   에서도 파란 틴트가 안 생기게.

교훈: G-Buffer 채널의 의미를 바꿀 때(여기선 gBuffer2.gba: 패딩→emissive)
**그 채널을 쓰는 모든 프로듀서**를 같이 고쳐야 한다. G-Buffer 프로듀서는
`gbuffer.frag`와 `gbuffer_nobindless.frag` 둘이었는데 하나만 고쳤다.

---

## 9. 네이티브 resize 크래시 (별개 잠복 버그)

### 증상

창 크기 조정 때마다 크래시.

### 원인

네이티브 resize 경로(`recreateSwapchain`, `handleFramebufferResize(w,h)`)의
`#else` 가지가 `recreatePostProcessResources()`만 호출하고, WebGPU 가지가
하던 **`gBufferPass->resize()` + `createDeferredLightingPass()`를 빠뜨렸다.**

1. `createRHIDepthResources()`가 옛 depth view를 해제·재생성
2. `GBufferPass::m_depthView`는 해제된 옛 view를 계속 가리킴 (dangling)
3. G-Buffer 컬러 타깃도 옛 해상도 그대로 → deferred bind group도 옛 view 참조
4. 다음 프레임에서 use-after-free → 크래시

deferred 렌더링이 네이티브에서 resize와 함께 테스트된 적이 없어 잠복해
있던 버그(내 변경과 무관).

### 수정 — resize 시 G-Buffer / deferred 재생성

두 네이티브 가지 모두에 WebGPU와 동일한 블록 추가:

```cpp
if (gBufferPass && gBufferPass->isInitialized() && rhiDepthImageView) {
    gBufferPass->resize(width, height, rhiDepthImageView.get());
    createDeferredLightingPass();  // 새 G-Buffer + depth view로 bind group 재생성
}
recreatePostProcessResources();
```

`createDeferredLightingPass()`는 현재 G-Buffer 뷰와 depth 뷰로 패스를 새로
만들므로 resize에 안전(WebGPU 경로가 이미 이렇게 함).

---

## 10. 수정 파일 요약 (2부)

| 파일 | 변경 |
| --- | --- |
| `src/rendering/InstancedRenderData.hpp` | `textureIndices` 슬롯 의미 주석 갱신 (x=normal y=MR z=emissive w=AO; baseColor는 roughnessAOPad.b) |
| `src/rendering/Renderer.hpp` | ShowcaseAsset에 CPU측 `ObjectData` 사본, 헬멧용 Linear `showcaseMaterialSampler` (둘 다 Vulkan) |
| `src/rendering/Renderer.cpp` | (1) `createBindlessResources()`를 `createGBufferPass()` 이전으로 이동 (2) `setShowcaseMesh`가 ObjectData CPU 사본 저장 (3) `uploadShowcaseMaterialTextures`에 Vulkan bindless 등록·인덱스 패치·objectBuffer 재업로드 경로 (4) 네이티브 resize 두 경로에 G-Buffer resize + deferred 재생성 |
| `shaders/gbuffer.vert.glsl` | tangent(world, unnormalized) + normal/MR/emissive/AO 인덱스 출력 (location 8–12) |
| `shaders/gbuffer.frag.glsl` | bindless 풀 PBR: TBN 노멀맵, MR(G/B), AO, emissive→`gBuffer2.gba` |
| `shaders/gbuffer_nobindless.frag.glsl` | `gBuffer2.gba` 방어 (alpha 1→0) |
| `shaders/deferred_lighting.frag.glsl` | `gb2.gba`에서 emissive 추출, 라이팅 후 `color += emissive` |
| `shaders/gbuffer.wgsl` | ObjectData 주석을 새 슬롯 의미와 일치 |

---

## 11. 후속 (갱신)

| 항목 | 상태 |
| --- | --- |
| AB sub-task 1–7 (PBR sampling) | ✅ |
| AB sub-task 9 (시연 통합) | ✅ (1부) |
| **Vulkan parity** | ✅ (이 2부) — 헬멧 풀 PBR + 네이티브 resize 안정화 |
| AB sub-task 8 (SceneNode 최소 구현) | ⬜ **다음** |
| C (TAA) | ⬜ AB 완전 종결 후 |

남은 한계: tangent.w(bitangent sign) 미저장 — 현재 Gram-Schmidt + cross로
B를 만들어 대부분의 에셋엔 충분하나 미러링된 UV에서 어긋날 수 있음.
emissive는 `gBuffer2`(RGBA8Unorm) LDR 클램프 — HDR 발광 미지원.
ORM-packed MR(단일 텍스처에 AO+rough+metal) 자동 분해 미구현 — 현재는
AO를 별도 슬롯으로 받음.

빌딩 색: bindless가 켜지며 빌딩은 의도된 3개 머티리얼 색(콘크리트/메탈/
글래스)으로 렌더링. 사용자 확인 후 이 동작 유지로 확정.
