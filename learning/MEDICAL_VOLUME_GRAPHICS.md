# 의료 영상 볼륨 렌더링 — 적용된 그래픽스 기술 학습 문서

**문서 목적**: Mini-Engine의 의료 볼륨 트랙(M1~M4)에서 사용한 그래픽스 기법을
**왜 의료 데이터에 필요한가**의 관점에서 정리한다. 코드 사용법은 [VIEWERS.md](
../docs/current/medical-volume/VIEWERS.md), 마일스톤 진행은
[MEDICAL_VOLUME_ROADMAP.md](../docs/current/medical-volume/MEDICAL_VOLUME_ROADMAP.md)
참조. 본 문서는 **알고리즘과 그 의료 임상적 맥락**에 집중한다.

**대상 독자**: 그래픽스 엔지니어로서 의료 영상 도메인에 진입하려는 사람.
일반 게임/시뮬레이션 렌더링 지식은 있지만 의료 영상 특수성(HU·DICOM·VRT 등)은
처음 접하는 수준.

---

## 0. 의료 영상 데이터가 그래픽스에 던지는 본질적 차이

먼저 일반 3D 렌더링(메시·텍스처·셰이딩)과 의료 볼륨 렌더링이 다른 이유를
정리한다. 이 차이가 이후 모든 기법 선택의 근거가 된다.

| 항목 | 일반 3D | 의료 볼륨 |
| --- | --- | --- |
| 데이터 표현 | 표면(메시) + 표면 텍스처 | **3D 그리드 voxel** (연속체) |
| 단위 | 무차원 색·재질 | **물리량**(HU = Hounsfield Unit, 또는 MR 강도) |
| 정밀도 | 8비트 LDR 충분 | **12~16비트 필수** — HU는 -1000~+3000 범위 |
| 해상도 | 4K 텍스처 = 16MB | **512³ ~ 1024³** 한 시리즈에 수백 MB~수 GB |
| 가시화 의도 | 사실적 표현 | **진단적 가치** — 의사가 보고 싶은 것을 강조 |
| 광 모델 | 표면 반사·굴절 | **참여 매질**(참여 매질·산란) |

핵심: 의료 볼륨은 **표면이 없다**. CT의 모든 voxel은 "이 위치에 1 cm² 당 몇 mg의
물질이 X선을 얼마나 흡수했는지"를 담은 연속 데이터다. 표면 메시로 변환(marching
cubes 등)할 수도 있지만 그 순간 원본의 미세 디테일 · 투명 매질 · 내부 구조가
손실된다. **임상 진단은 "내부를 보는 행위"**라서 표면화는 부적합하다. 그래서
direct volume rendering — voxel을 직접 적분하는 방식 — 이 표준이 된다.

---

## 1. 데이터 표현: R16Float + Window/Level (M1)

### 임상 맥락

CT 데이터는 **Hounsfield Unit**으로 저장된다 (공기 = -1000 HU, 물 = 0,
연조직 = 40, 뼈 = 1000+). 이 12비트 신호를 1바이트로 양자화하면 정밀도가 무너진다.
8비트는 256 레벨밖에 없어 폐 텍스처(-800~-500 HU = 300 HU 범위)가 픽셀 단위 jaggy
가 되거나, 뼈 디테일이 한 레벨에 묶여버린다.

또 의사는 **윈도우/레벨**(window/level)이라는 임상 표준 도구로 같은 볼륨을
다양한 콘트라스트로 본다:

| 윈도우 프리셋 | center | width | 임상 용도 |
| --- | --- | --- | --- |
| Bone | +300 | 1500 | 뼈 구조 |
| Soft tissue | +40 | 400 | 장기·근육 |
| Lung | -600 | 1500 | 폐 실질 |
| Brain | +40 | 80 | 뇌실질·종양 |

이건 단순 채도 조절이 아니라 **다른 정보를 보기 위한 임상적 lookup**이다. 같은
CT에서 8비트 양자화 후 윈도잉을 하면 "이미 잃어버린 정밀도를 stretch만 하는 꼴"
이 된다.

### 적용한 그래픽스 기술

**R16Float 3D 텍스처** + 셰이더에서 윈도우 변환.

- 원본 16비트 HU → R16Float (half-float)로 저장. CT의 -1000~+3000 범위를 정확히
  표현 (half-float은 ±2¹⁶까지 정확).
- 셰이더에서: `n = clamp((raw - (center - width/2)) / width, 0, 1)` — 윈도우
  범위를 [0,1]로 매핑. 그 위에 transfer function(LUT)을 적용해 색·불투명도 결정.
- 양 백엔드 코어 + 필터러블 포맷 교집합으로 R16Float 채택 (R16Unorm은 WebGPU
  코어 아님). 이 결정 자체가 **듀얼 백엔드 패리티 원칙**의 첫 사례.

코드: [`volume_march.frag.glsl:124-127`](../shaders/volume_march.frag.glsl),
[`VolumeRenderer::loadFromFloatData`](../src/rendering/VolumeRenderer.cpp).

### 트레이드오프

- HU 손실 0, 윈도잉 자유.
- 메모리: 1024³ × 2바이트 = 2GB. 8비트 대비 2배 — M3 sparse storage(아래)로 상쇄.

---

## 2. Direct Volume Rendering — 레이마칭의 기본 (M1)

### 임상 맥락

ImagePile의 X선 흡수율 그래프를 카메라 광선을 따라 **front-to-back으로 적분**하면
의료 영상에서 익숙한 "MIP·반투명 합성 영상"이 된다. 광선이 voxel을 차례로 지나며
"여기 얼마나 흡수하는가?"를 누적해 픽셀 색·불투명도를 결정한다.

### 적용한 그래픽스 기술

**Beer-Lambert 흡수 모델 + Premultiplied front-to-back compositing**.

- 카메라 광선 vs 볼륨 AABB 교차로 [tNear, tFar] 구간 결정 (slab test).
- 균일 step으로 행진. 각 step에서:
  1. World → volume UV 변환 → `sampleVolume(uvw)`로 raw HU 샘플
  2. 윈도우/레벨로 [0,1] 정규화 → transfer function 적용 → (color, opacity)
  3. Beer-Lambert: `alpha = 1 - exp(-opacityWeight × extinction × stepSize)`
  4. premultiplied over: `accum.rgb += (1-accum.a) × color × alpha`
- 광학 깊이가 충분히 누적되면 (`accum.a > 0.99`) 조기 종료 (early ray termination).

코드: [`volume_march.frag.glsl`](../shaders/volume_march.frag.glsl) main loop.

### 왜 premultiplied인가

"이미 누적된 a가 1에 가까울수록 새 샘플의 영향이 줄어든다"는 over 연산자를
`accum.rgb + (1-accum.a) × color × alpha` 한 줄로 표현 가능 — 블렌딩 하드웨어와
직접 매핑된다. 비-premultiplied로 가면 매 step마다 분리된 곱셈이 늘어난다.

### 트레이드오프

- step 크기 ↓ → 정확도 ↑, 비용 ↑. step은 일반적으로 0.5~1 voxel 크기.
- step 균일 → AABB 가장자리에서 aliasing. jittered start로 완화 가능.

---

## 3. Gradient 셰이딩 — voxel에서 "법선"을 만드는 법 (M2-1)

### 임상 맥락

흡수만 적분하면 평평한 회색 덩어리가 나온다. **입체감**이 없다. 의사는
입체감을 통해 종양·혈관의 3D 형상을 인지하므로, 표면 없는 데이터에 **유사 표면
셰이딩**을 입혀야 한다.

### 적용한 그래픽스 기술

**중앙차분 gradient = surface normal**.

매 샘플에서:

```glsl
vec3 g;
g.x = sampleVolume(uvw + vec3(e,0,0)) - sampleVolume(uvw - vec3(e,0,0));
g.y = sampleVolume(uvw + vec3(0,e,0)) - sampleVolume(uvw - vec3(0,e,0));
g.z = sampleVolume(uvw + vec3(0,0,e)) - sampleVolume(uvw - vec3(0,0,e));
vec3 normal = -g / length(g);   // -g: 밀도 감소 방향이 바깥
float ndl = max(dot(normal, L), 0.0);
color *= ambient + diffuse × ndl;
```

`e`는 한 voxel 크기 (`1.0 / maxDim`).

### 왜 작동하는가

밀도 = 스칼라 필드 φ. 등밀도 표면(isosurface)의 법선은 ∇φ. 중앙차분으로 ∇φ를
근사. 표면이 명시적으로 없어도 **gradient가 표면을 정의**.

### 트레이드오프

- 샘플당 +6 density read (cache locality 덕에 bricked storage와 시너지)
- 노이즈가 많은 데이터에선 gradient도 noisy → 부드러운 음영이 안 나옴. denoise
  또는 multi-scale gradient 필요 (M2 v1 후보).

---

## 4. 볼류메트릭 소프트 섀도우 — Self-Shadowing (M2-2)

### 임상 맥락

뼈 옆 연조직, 폐의 미세 구조 — **같은 매질이 자신을 가리는** 효과가 입체감의
핵심. 표면 없는 매질에서도 그림자가 가능하다는 게 시네마틱 VRT의 핵심.

### 적용한 그래픽스 기술

**Secondary ray로 광원까지의 광학 깊이 누적**.

매 샘플 위치 wp에서:

```glsl
float tau = 0.0;
for (int s = 0; s < shadowMaxSteps; ++s) {
    vec3 suvw = (wp + L × st - aabbMin) / boxSize;
    if (out of bounds) break;
    float sd = sampleDensity(suvw);
    tau += sd × extinction × strength × stepSize;
    if (tau > 8.0) break;   // 사실상 완전 차폐
    st += stepSize;
}
float shadowF = exp(-tau);
color *= ambient + diffuse × ndl × shadowF;
```

각 primary 샘플마다 광원 방향으로 mini-raymarch 수행 → 그 경로의 광학 깊이를
exp(-τ)로 환산해 음영에 곱.

### 왜 WebGL이 어려운가

샘플당 N개의 추가 read = N^2 비용. WebGL은 **컴퓨트 셰이더가 없어** 매 프레임
전역 광량 누적 패스를 사전 계산할 수 없다. WebGPU는 compute로 light propagation
volume류 최적화를 사전 빌드 가능 (M3-1 occupancy와 같은 결의 작업) — 이 지점이
WebGPU가 WebGL을 본질적으로 앞서는 첫 영역.

### 트레이드오프

- 가장 비싼 셰이딩 효과. 1024³ 풀 해상도에서 step 24 = 약 30% 프레임 시간 소비.
- empty-space skipping(M3-1)과 결합하면 광원 방향 광학 깊이 0인 셀을 건너뜀.

---

## 5. Empty-Space Skipping — 의료 데이터의 압도적 sparsity 활용 (M3-1)

### 임상 맥락

CT 한 시리즈의 voxel 중 **70~95%는 공기(-1000 HU)**. 환자 외 모든 공간은 무가치
하다. 이 sparsity를 활용 안 하면 비용 대부분이 air 적분에 소모된다.

### 적용한 그래픽스 기술

**Compute 셰이더로 사전 빌드한 macro-cell occupancy grid**.

빌드(사전, 한 번):
- 볼륨을 8³ macro-cell로 분할 (compute workgroup 4×4×4)
- 각 셀에서 그 셀이 포함하는 64 voxel을 스캔 → (min, max) 강도 저장
- 결과: `vec2[gridW × gridH × gridD]` storage buffer

마치(per frame):
```glsl
float cmax = occCells[cellIdx].y;
float windowed = clamp((cmax - (winCenter - winWidth/2)) / winWidth, 0, 1);
if (windowed × densityScale - threshold <= 0.0) {
    // 셀 출구까지 t를 점프
    t = cellExitT + stepSize × 0.5;
    continue;
}
```

### 왜 이게 GPU 컴퓨트의 영역인가

CPU로 빌드하면 1024³ 볼륨에서 그리드 (128,128,128) = 2M 셀 × 셀당 512 voxel
스캔 = 1G read. CPU는 분 단위. **Compute 셰이더에서는 workgroup 단위 병렬로
1초 미만**. 또 occupancy는 **윈도우/레벨이 바뀔 때마다 재빌드 가능** (셀의
min/max는 윈도우 독립적이고 셰이더에서 윈도잉만 적용하기 때문).

### 시도한 최적화와 그 함정 (CHANGELOG 2026-05-31)

per-cell-entry 변형 — 셀에 처음 들어올 때 한 번만 검사, 셀 안에서는 검사 생략.
**warp divergence로 회귀**. CPU 직관과 반대: per-sample 검사는 cache가 read를
무료화해주는 패턴 (인접 픽셀이 같은 셀에 있으므로 SIMD coherent). per-entry는
경계 stride가 다양해져 발산 → 되돌림.

---

## 6. Brick Atlas + Page Table — 대용량 볼륨의 sparse 저장 (M3-3 v0)

### 임상 맥락

1024³ × 2바이트 = 2GB. 브라우저 WebGPU 메모리 한계(보통 4GB) 안에 들어가지만 한
시리즈가 그 거의 절반을 잡아먹으면 다른 작업 못 함. 그러나 **그 2GB의 70%+는
공기**라면 저장 자체를 안 할 수 있다.

게임 엔진에서는 **sparse virtual texture** (id Tech 5의 megatexture, Carmack)나
**sparse voxel octree** (Crassin)로 같은 문제를 풀어왔다. 의료 볼륨에 이식.

### 적용한 그래픽스 기술

**Brick atlas + page table 간접 참조**.

저장 구조:
- 원본 볼륨을 **64³ brick** 단위로 분할 (pageGrid = ceil(volSize/64))
- 빈 brick(내부가 전부 air)은 atlas에 안 올림
- 비어있지 않은 brick만 **brick atlas 3D 텍스처**의 slot에 packed로 저장
  - 각 slot은 **66³** (1 voxel halo 양면) — linear filter가 brick 경계를 넘어
    설 때 인접 brick voxel을 read하기 위함
- **Page table** (storage buffer of `uint32`): virtual brick 좌표 → atlas slot
  인덱스 또는 `0xFFFFFFFF` (sentinel = "air")

샘플링:
```glsl
float sampleVolume(vec3 uvw) {
    vec3 vp = uvw × volSize;
    ivec3 brickIdx = ivec3(vp) / 64;
    vec3 local = vp - vec3(brickIdx × 64);  // [0, 64) 내부 좌표
    uint slot = pageSlots[brickIdx.z × pageGrid.y × pageGrid.x + ...];
    if (slot == 0xFFFFFFFFu) return 0.0;
    // slot을 atlas 3D 좌표로 unpack
    vec3 atlasUvw = ... slot 기반 atlas 좌표 + local + halo offset ...;
    return texture(volumeAtlas, atlasUvw).r;
}
```

### 핵심 트레이드오프

- **메모리 절감**: 1024³ × 10% non-empty → 2GB → ~200MB
- **샘플당 비용 ↑**: 1 → 2 indirection (page table read + atlas read). 단,
  page table은 작아 cache hit 잘 됨, atlas는 spatial coherent.
- **Halo 오버헤드**: 64³ vs 66³ = 9% 저장 증가 (양면 1 voxel halo). linear filter
  정확성을 위한 비용.

### 자동 atlas 크기 산정 (Step C)

볼륨이 워낙 다양해서 atlas 크기를 호출자에게 떠넘기면 부담. M3-3 v0의 빌드는:
1. pageGrid를 계산하고
2. `atlasGrid = min(pageGrid, 8)` per axis (cap = 512 slots ~ 292MB)
3. atlas 초과 시 모든 non-empty brick을 카운트 → `ceil(cbrt(N))`으로 권장
   atlasGrid를 로그로 안내 → 호출자가 즉시 override

### 다음 (M3-3 v1)

현재는 **로드 타임 단발 packing**. 진정한 "RAM/VRAM 초과" 1GB+ 시리즈는
**streaming** 필요: camera frustum 가시성 → 필요한 brick만 atlas에 LRU 업로드,
디스크/원본 버퍼에서 페이지인. id Tech의 megatexture 또는 Sparse Resource
(Vulkan VK_EXT_sparse_residency)의 의료 영상 적용 — 다음 마일스톤의 본체.

---

## 7. Path Tracing — 진짜 시네마틱 VRT (M4 v0)

### 임상 맥락

지금까지의 모든 기법(흡수 + Lambert + 그림자)은 **단일 산란** 모델. 실제 빛은
조직 안에서 수천 번 산란하고 일부는 반대로 돌아온다. 이 다중 산란이 만드는
"내부 발광 같은 질감"이 **임상 워크스테이션의 cinematic VRT**의 핵심.

Siemens Cinematic, Philips Illuminate3D — 모두 GPU compute path tracer로 구현
되어 있고, 진단 워크플로우에 진입했다. WebGPU compute는 이를 브라우저로 끌어
오는 첫 가능성.

### 적용한 그래픽스 기술

**Woodcock(delta) tracking + Henyey-Greenstein phase function + single-light NEE**.

**1. Woodcock free-flight tracking**: 비균질 매질의 자유 비행 거리 샘플링. 균일
매질의 σ_max로 stride를 잡고, 매 점에서 `random() < σ(x) / σ_max`이면 진짜
상호작용, 아니면 fictitious(가짜) 상호작용으로 더 진행. 무한 그리드 적분 없이
unbiased 자유 비행 거리 획득.

```glsl
while (...) {
    t -= log(rnd()) / sigmaMax;
    if (rnd() < sampleDensity(uvw) × extinction / sigmaMax) {
        // 진짜 산란 발생
        break;
    }
    // 가짜 상호작용: 계속 진행
}
```

**2. Henyey-Greenstein phase function**: 산란 방향 분포. 파라미터 g ∈ [-1, 1]:
- g > 0: 전방 산란 (생물학적 조직, 빛이 진행 방향 유지)
- g = 0: isotropic
- g < 0: 후방 산란

조직은 g ≈ 0.7~0.9 (강한 전방 산란). 슬라이더로 사용자가 조정.

**3. Single-light NEE (Next Event Estimation)**: 산란 시점에서 광원에 곧장
ray를 보내 직접광 기여를 분리 평가. 다중 산란 + 직접광을 양쪽에서 모은다.

### 왜 fragment 셰이더로 구현했나

이상적으로는 compute. 그러나 v0 단계에서 fragment를 쓴 이유:
- swapchain target에 직접 누적 쓰기 가능 (텍스처 binding 복잡도 ↓)
- 픽셀당 독립 — fragment의 자연스러운 병렬성과 일치
- compute 마이그레이션은 v1+ 후보 (workgroup shared memory로 brick prefetch 등
  최적화 여지가 있을 때).

### v0의 한계

매 프레임 독립 SPP 평균 → **노이즈 항상 보임**. 카메라 정지해도 수렴 안 됨.
v1에서 progressive accumulation으로 해결 (다음 섹션).

---

## 8. Progressive Temporal Accumulation — 정지 시 시네마틱 (M4 v1)

### 임상 맥락

의사는 종양 부근에서 **카메라를 멈추고 정지 이미지를 본다**. 그 순간 노이즈
없는 깨끗한 시네마틱 영상이 나와야 진단 가치가 있다.

### 적용한 그래픽스 기술

**Ping-pong RGBA16Float 텍스처에 running mean 누적**.

각 프레임:
1. Pass 1 (path-trace) → 새 sample을 `current` 색으로 계산
2. 이전 누적(history) read: `prev = texelFetch(historyTex, fragCoord)`
3. Blend: `blended = (prev × N + current) / (N + 1)` (running mean)
4. blended를 history의 OTHER ping-pong slot에 write
5. Pass 2 (display) → blended를 Reinhard tonemap → swapchain

호스트가 카메라/파라미터/리사이즈 변화 감지 시 `N = 0` reset. **N = 0이면
`prev × 0 = 0`이라 history 텍스처 명시 clear 불필요** — 수식이 자연스럽게
새 시작을 만든다.

### 왜 tonemap이 나중인가

Reinhard는 비선형. 비선형 함수를 평균에 넣으면 잘못된 결과. 반드시:
```
linear samples → 평균 → tonemap
```
순서. 만약 path-trace에서 매 프레임 tonemap → 평균하면 정지해도 영원히 비선형
편향이 남는다.

### 듀얼 백엔드의 GLSL/WGSL 차이

- Vulkan GLSL: `texelFetch(sampler2D(historyTex, sampler))` — sampler 바인딩
  필요.
- WebGPU WGSL: `textureLoad(historyTex, fragCoord)` — sampler 미사용.

같은 알고리즘이지만 bind group layout이 백엔드별로 갈린다. RHI 추상화에서
binding 명세가 명시적이라 가능 — `#ifdef __EMSCRIPTEN__` 분기로 처리.

---

## 9. 듀얼 백엔드 RHI — Vulkan + WebGPU 패리티 유지

### 왜 듀얼 백엔드인가

- **Vulkan**: 개발·검증·프로파일링의 1차 백엔드. 검증 레이어, RenderDoc, GPU
  타이머의 정밀도. 알고리즘 정확성을 여기서 닫는다.
- **WebGPU**: 배포·시연의 차별화. 환자에게, 의사에게 즉시 보여줄 수 있는 채널.
  서버 GPU 비용 0.

같은 코드가 양쪽에서 동작해야 가치 — 한쪽만 굴러가면 둘 다 진짜는 아니다.

### RHI 추상화의 핵심 결정

- **포맷은 교집합**: R16Float, BGRA8UnormSrgb, RGBA16Float — 양 백엔드 코어.
  R16Unorm 같은 한쪽 전용 포맷은 금지.
- **Bind group는 명시적**: 추상화에서 layout과 entry를 분리 선언 (Vulkan
  descriptor set과 WebGPU bind group 양쪽에 매핑 가능).
- **셰이더 쌍**: `*.frag.glsl` + `*.wgsl` 쌍을 같은 의미로 유지. UBO struct는
  동기화 필수. M3-3 v0 작업 시 GLSL march의 UBO에서 누락된 `pathtrace` vec4를
  발견 → 정렬 (이전엔 인덱스 어긋남으로 garbage read).
- **WebGPU 256B 정렬 (CLAUDE.md §9)**: 텍스처 업로드의 `bytesPerRow`는 WebGPU
  에서 256바이트 배수 필수. 1×1 텍스처도 예외 없음. `ResourceManager::
  uploadRGBA8FromMemory`가 표준 패턴.

### Vulkan-permissive vs Dawn-strict (CHANGELOG 2026-06-02)

같은 코드가 한쪽에서 통과한다고 안전하지 않다. M3-3 v0에서 OccUBO 버퍼를 32 →
64바이트로 키웠지만 bind group entry의 binding size는 32바이트 그대로 남겼고,
Vulkan은 silently 통과 (garbage read를 0으로 반환), Dawn은 strict 에러로 잡았다.
**듀얼 백엔드 검증은 "시각 OK Vulkan" 만으로 부족** — WebGPU 콘솔까지 깨끗해야
한다.

---

## 10. 종합 — 한 시리즈가 어떻게 픽셀이 되는가

```
[디스크 .dcm/.nii]
   ↓ AssetImporter (cgltf/NiftiFile/DicomFile)
[CPU Volume3D 구조 — intensity[], spacing, origin]
   ↓ VolumeRenderer::loadFromFloatData (R16Float half 변환)
[BrickedVolume::build — 빈 brick 제거, atlas + page table 업로드]
   ↓
[occupancy compute — sparse 그리드 빌드, GPU]
   ↓
[매 프레임:]
   카메라 ray → AABB 교차
   ↓
   march pass (Lambert mode):
     - 균일 step
     - empty-space skip 체크 (occupancy)
     - sampleVolume(uvw) — page table → atlas
     - window/level → TF LUT
     - gradient shading (volumetric soft shadow)
     - premultiplied over compositing
   ↓
   path-trace pass (PT mode):
     - Woodcock free-flight
     - HG phase, NEE
     - linear HDR을 history에 누적 (running mean)
   ↓
   display pass (PT mode):
     - history → Reinhard tonemap → swapchain
   ↓
[픽셀]
```

각 단계가 의료 데이터의 특수성 — HU 정밀도·sparsity·임상 윈도잉·시네마틱
입체감 — 에 맞춰 선택된 그래픽스 기법이다.

---

## 11. 추가 학습 자료 (외부)

- **Direct Volume Rendering**: Engel et al, *Real-Time Volume Graphics*
  (CRC Press, 2006). 거의 모든 기초 기법 정리.
- **Sparse Voxel Storage**: Crassin et al, *GigaVoxels* (2009). SVO 기반
  스트리밍 — M3-3 v1의 참고점.
- **Volumetric Path Tracing**: Novák et al, *Monte Carlo Methods for
  Volumetric Light Transport Simulation* (Eurographics 2018 STAR).
  Woodcock·HG·NEE의 표준 정리.
- **Cinematic VRT (의료)**:
  - Comaniciu et al, *Photorealistic rendering for clinical case
    presentation* — Siemens Cinematic Rendering 백서.
  - Eid et al, *Cinematic Rendering in CT: Concepts and Clinical
    Applications* (Radiology 2017).

코드 진입은 [VIEWERS.md](../docs/current/medical-volume/VIEWERS.md), 전략은
[MEDICAL_VOLUME_ROADMAP.md](../docs/current/medical-volume/MEDICAL_VOLUME_ROADMAP.md)
참조.
