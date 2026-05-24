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
