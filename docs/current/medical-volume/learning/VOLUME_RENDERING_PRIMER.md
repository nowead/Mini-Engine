# 볼륨 렌더링 입문 — Mini-Engine 의료 트랙 학습 가이드

> 최신화: 2026-06-17 (M1~M3 완료 + M4 v0/v1/v2 P1·P2.2 까지)
> 대상: 볼륨 렌더링이 처음이지만 그래픽스 기본기 (래스터화, 셰이더, 행렬, RHI)
> 는 있는 독자.

이 문서는 **개념 → Mini-Engine 구현 → 실제 파일 위치** 순으로 따라가는
볼륨 렌더링 입문서다. 표면 렌더링과 무엇이 다른지, 의료 데이터를 어떻게
처리하는지, 1024³ 볼륨이 어떻게 GPU 위에서 작동하는지, 그리고 path tracing
이 표면 머티리얼 없이 어떻게 사진 같은 볼륨 영상을 만드는지를 모두 한 곳에서
설명한다.

---

## 0. 큰 그림

게임 엔진에서 익숙한 것 — 삼각형 메시, 표면 머티리얼, 라이팅 — 은 다 **표면**
이야기다. 의료 볼륨은 다르다. 입력은 3D 그리드 안의 *밀도값* (CT 의 HU 단위
또는 MR 의 임의 강도). 표면 같은 건 없다. 우리가 풀어야 할 식은:

```text
픽셀 색 = 카메라 광선이 볼륨을 지나가며 만난 모든 voxel 의 기여 적분
       + 광원이 산란점에 도달하는 정도
       + 멀티 산란 (path tracing 모드에서만)
```

Mini-Engine 의료 볼륨 트랙은 이 식을 두 가지 모드로 푼다:

- **Lambert + 레이마칭** — 단일 산란 근사. 빠르고 안정적. M1~M3 기본 모드.
- **Path-traced** — Woodcock 자유경로 + Henyey-Greenstein 위상함수 + NEE +
  progressive temporal accumulation. M4 트랙. 카메라 정지 시 수렴.

이 입문서는 두 모드 모두를 다룬다.

---

## 1. Voxel 에서 Pixel 까지 — 레이마칭

### 1.1 카메라 광선 만들기

화면 픽셀 → 월드 광선:

1. 픽셀 좌표 → NDC `(x, y) ∈ [-1, +1]`.
2. NDC → 뷰 공간: `inverseProj × (x, y, 1, 1)`, perspective 나눔.
3. 뷰 공간 → 월드: `inverseView × viewSpacePoint`.
4. 광선 = `cameraPos` 에서 그 월드 점으로의 단위 벡터.

`shaders/volume_march.wgsl:vs_main` 이 풀스크린 삼각형 만들고, fragment 가
`gl_FragCoord` 로 위 과정을 수행.

### 1.2 광선 위에서 sampling

볼륨 AABB 와 ray-box 인터섹션 → 광선이 볼륨을 통과하는 [tNear, tFar] 구간 결정.
그 구간을 일정한 step size 로 전진하며:

1. 현재 월드 위치 → 텍스처 UVW (= `(p - aabbMin) / aabbSize`).
2. `sampleVolume(uvw)` → R16Float voxel 값 (raw intensity).
3. window/level 로 정규화 → 색·알파 결정 → 적분.

step 한 번에 얼마나 전진할지가 핵심 trade-off:

| step 작음 | step 큼 |
| --- | --- |
| 정확도 ↑ | 정확도 ↓ |
| 비용 ↑ (1024³ 가시 brick × 수십 step) | 비용 ↓ |
| 미세 구조 보존 | 미세 구조 누락 |

Mini-Engine 의 default: `stepSize = 0.01` (월드 단위). UI 슬라이더로 조정.

### 1.3 Beer-Lambert (감쇠)

광선이 농도 `σ_t` 인 매질을 거리 `t` 통과하면 그 사이 빛이 줄어든다:

```text
T(t) = exp(-σ_t × t)
```

이산화하면 각 step 의 투과율을 곱해간다. Mini-Engine 의 front-to-back
compositing 식:

```wgsl
let stepAlpha = 1.0 - exp(-density * extinction * stepSize);
outColor += (1.0 - outAlpha) * src.rgb * stepAlpha;
outAlpha += (1.0 - outAlpha) * stepAlpha;
if (outAlpha > 0.99) break;   // 조기 종료
```

- `density` — TF 적용 후 정규화 강도.
- `extinction` — UI 슬라이더 (기본 10.0).
- 조기 종료 — 알파가 99% 이상이면 뒤는 안 보이니 멈춤.

### 1.4 셰이더 매핑

`shaders/volume_march.wgsl` (WebGPU) 와 `volume_march.frag.glsl` (Vulkan)
이 미러. UBO 레이아웃은 `VolumeRenderer::VolumeUBO` (`src/rendering/VolumeRenderer.hpp`)
의 1:1 미러. 매 프레임 viewer 가 `updateUBO()` 로 갱신.

---

## 2. Transfer Function — voxel 을 색으로

raw voxel 값은 의미 모름 (HU? raw int? slope/intercept 적용 전?). 화면에
의미 있는 색·투명도를 주려면 두 단계 변환이 필요하다.

### 2.1 Window/Level (선형 재매핑)

임상의 표준 정규화:

```text
n = clamp((raw - (center - width/2)) / width, 0, 1)
```

`center` = 관심 HU 중심, `width` = 폭. preset:

- **폐 (Lung)**: center=-600, width=1500
- **뼈 (Bone)**: center=+300, width=1500
- **연조직 (Soft Tissue)**: center=+40, width=400
- **MR-T1 / MR-T2**: 데이터 의존

`loadFromFloatData` 가 로드 직후 auto-fit (center = (min+max)/2, width=
max-min) 으로 설정. viewer 가 modality 별 preset 으로 덮어쓴다.

비-CT 데이터 (매머그래피, MR) 는 dataMin >= 0 이라 HU 본 윈도우 (300/1500)
적용 시 거의 투명 → viewer 가 `dataMin < -500` 일 때만 HU 본 윈도우 적용
하도록 보호 (`a3a4133` 커밋).

### 2.2 LUT (256×1 RGBA 텍스처)

정규화 `n ∈ [0,1]` → 색·알파:

```text
src = textureSample(tfLUT, vec2(n, 0.5))
```

LUT 은 piecewise-linear 키 4~5 개에서 CPU 가 생성 (`fillFromKeys()` in
`VolumeRenderer.cpp`). preset 변경 시 `m_tfDirty=true` 마크 → 다음 프레임
시작에 `applyPendingTFUpdate()` 가 업로드.

7개 preset:

- **Custom** — 두 색 그래디언트 (LUT 미사용 경로).
- **Cloud** — 거의 흰 톤, smooth alpha ramp (X-ray 느낌).
- **Fire** — 어두운 보라 → 주황 → 노랑 → 흰색 (그래디언트 시각화).
- **CT-Bone** — 0.55 까지 투명, 그 위부터 본 색 (HU bone window 전용).
- **CT-Soft Tissue** — mid-range 강조 (연조직 highlight).
- **MR-T1** — CSF dark → WM bright.
- **MR-T2** — CSF bright → WM dark.

비-CT 데이터에는 viewer 가 Cloud(1) 디폴트로 시각적 안정성 보장.

### 2.3 Density threshold + density scale

LUT 사용 전 추가 가공:

```wgsl
var density = n * densityScale;      // 강도 스케일
density = max(density - threshold, 0);  // 임계치 cut-off
```

`densityScale` (기본 1.5), `threshold` (기본 0.05). UI 로 조정. cut-off 가
TF 의 첫 키 (보통 alpha=0) 위에서 노이즈 cleanup 역할.

---

## 3. 그래디언트 셰이딩 — 입체감 복원

표면이 없으니 법선이 없다. 그래도 입체감을 주는 방법:

### 3.1 밀도 그래디언트 = 표면 법선

이산 중심 차분:

```text
∇density(p) ≈ vec3(
    density(p + dx) - density(p - dx),
    density(p + dy) - density(p - dy),
    density(p + dz) - density(p - dz)
) / (2 × eps)
```

이 벡터는 *밀도 증가 방향*. 이를 법선으로 쓰면 (정규화), Lambert 라이팅이
가능:

```wgsl
let N = normalize(gradient);
let lambert = max(dot(N, lightDir), 0.0);
finalColor = src.rgb * (ambient + diffuse * lambert);
```

`gradEps` 가 텍스처 공간 step (UV 단위 ~1 voxel). UI 토글 (Gradient shading)
로 on/off.

### 3.2 볼류메트릭 소프트 섀도우

각 step 에서 광원 방향으로 보조 광선 발사 → 매질 두께 누적 → 자기 그림자.

```text
shadowTransmittance = exp(-Σ shadowStep × densityAlongShadowRay × strength)
finalColor *= shadowTransmittance
```

비용: 1 step × 24 shadow step ≈ 24 × density sample 추가. UI 슬라이더로
shadow step / max steps / strength 조정.

`shaders/volume_march.wgsl:185-225` 가 두 기능 모두 실장.

---

## 4. Empty-Space Skipping — 공기 그냥 지나가지 마

### 4.1 Occupancy 그리드

볼륨 전체를 셀 단위 (예: 8 voxel) 로 쪼개, 각 셀에 그 안에 비-air voxel 이
있는지 1-bit 마크. 셰이더는 *경계 셀* 만 마칭, 빈 셀은 통째로 skip.

### 4.2 컴퓨트로 build

`shaders/volume_occupancy.comp.wgsl` 가 page table 을 순회 → brick 단위로
windowed density max 계산 → 0 인 셀을 표시. 매 프레임 자동 갱신.

### 4.3 march 루프에서 skip

```wgsl
// 현재 위치의 occupancy cell 확인
let cellIdx = vec3<i32>(uvw * occGridSize);
if (occGrid[cellIdx] == EMPTY) {
    // 다음 비-empty 셀까지 점프
    t = jumpToNextNonEmptyCell(...);
    continue;
}
```

A/B 토글로 성능 비교. sparse 256³ 합성에서 3~5% 개선 (정직한 측정 —
shading cost 우세). 더 dense 데이터에서 효과 커짐.

---

## 5. Brick Atlas + Page Table — 큰 볼륨을 GPU 에

### 5.1 Bricks

1024³ × 2B = 2 GB 단일 텍스처는 비현실. 볼륨을 64³ voxel **brick** 으로
쪼갠다. 1024³ → 4096 brick.

### 5.2 Atlas + page table

- **Atlas 텍스처** — brick 데이터를 한 3D 텍스처 안에 packed slot 으로 저장.
  Atlas grid 가 X×Y×Z brick capacity. 총 slot 수 = X × Y × Z.
- **Page table** — page table 의 한 항목 = `(lod << 30) | slot`. `0xFFFFFFFF`
  = empty (공기). storage buffer of uint32.

매 voxel sample 이 두 lookup:

1. `pageIdx = brick.z × pageGrid.x × pageGrid.y + brick.y × pageGrid.x + brick.x`
2. `pageSlots[pageIdx]` → `(lod, slot)` 또는 sentinel.
3. slot 인덱스 → atlas 좌표 (`slot % atlas.x`, ...).
4. atlas 안 좌표 + halo offset → 실제 texel.

### 5.3 Halo (1-voxel) — sampling 정확성

trilinear sampling 은 8 voxel 본다. brick 경계에서 이웃 brick voxel 이
필요하지만 atlas 안에선 다른 위치. 해법: 각 brick 을 66³ (interior 64³ +
halo 1 voxel 사방) 으로 저장. atlas 메모리 +6.5% 비용으로 정확도 보장.

### 5.4 셰이더가 보는 것

3개 셰이더 (`volume_march`, `volume_pathtrace`, `volume_occupancy`) 가 모두
같은 `sampleVolume(uvw) -> f32` 헬퍼 사용. 헬퍼 내부에서 page lookup → atlas
좌표 변환 → 실제 텍스처 sample.

```wgsl
fn sampleVolume(uvw: vec3<f32>) -> f32 {
    let vp = clamp(uvw, ..., ...) * volSize;
    let brickIdx = vec3<i32>(vp) / 64;
    let pageIdx = brickIdx.z * pageGrid.y * pageGrid.x + ...;
    let page = pageSlots[pageIdx];
    if (page == 0xFFFFFFFFu) { return 0.0; }
    let slot = page & 0x3FFFFFFFu;
    let lod = page >> 30u;
    // ... atlas 좌표 계산, lod 별 텍스처 분기
    return textureSampleLevel(volumeTex<LOD>, sampler, atlasUvw, 0.0).r;
}
```

이 추상화 덕분에 v0 단순 slot → v1-β multi-LOD 전환 시 사용처 코드 무변경.

---

## 6. LOD — Multi-Resolution Bricks

### 6.1 4 단계 LOD

각 brick 의 4 단계 다운샘플:

| LOD | brick voxel | atlas 비중 |
| --- | --- | --- |
| L0 | 64³ (원본) | 100% |
| L1 | 32³ | 12.5% |
| L2 | 16³ | 1.56% |
| L3 | 8³ | 0.20% |

4 단계 모두 build 해도 atlas 메모리 +16% (~1/8 + 1/64 + 1/512). 작은 비용
으로 거리 기반 fallback 가능.

### 6.2 거리 기반 선택

매 프레임:

```text
distance = length(brickCenter - cameraPos)
if      (distance > thresh3) lod = 3
else if (distance > thresh2) lod = 2
else if (distance > thresh1) lod = 1
else                         lod = 0
```

임계값은 fov + 화면 픽셀-당-voxel 비율로 정밀화 가능 (β-6 후순위).
현재는 단순 distance threshold.

### 6.3 Page table 인코딩

상위 2비트 = LOD, 하위 30비트 = slot. 셰이더가 디코드 → 적절한 lod
atlas (`volumeTex0~3`) 샘플링.

### 6.4 LOD fallback

선택한 LOD 의 atlas slot 이 가득 차서 못 올라가면 한 단계 coarser LOD 로
fallback. 결과: 줌아웃 케이스 missing brick **2320 → 326 (-86%)**.

### 6.5 정직한 한계 — LOD seam

인접 brick 의 LOD 가 다르면 sampling 불연속 → 시각 seam 아티팩트.
dual-LOD blending (인접 두 LOD 동시 sample 후 보간) 으로 본질 해결 가능,
하지만 성능 ×2. 현재 트랙 후순위.

---

## 7. Streaming — Atlas 가 가시 brick 다 못 담을 때

### 7.1 Frustum cull

매 프레임 가시 brick 집합 계산:

1. brick 경계 box vs frustum 평면 6 개 → intersect 검사.
2. occupancy grid 와 교차 → non-empty 가시 brick.

### 7.2 LRU eviction

`BrickedVolume::updateStreaming` 가 LRU 정책으로 atlas slot 관리:

- 가시 brick + atlas 에 있음 → bump LRU 타임스탬프.
- 가시 brick + atlas 없음 → upload queue 에 추가.
- 비-가시 brick + atlas 있음 → eviction 후보.

**Visible 보호**: 이번 프레임 가시 brick 의 slot 은 evict 금지 → churn
방지.

### 7.3 매 프레임 K 개 제한

K=8~64 (UI 슬라이더). 한 프레임에 너무 많이 올리면 staging buffer
폭주 + frame time spike. K 가 작으면 점진 페이지인.

### 7.4 LOD migration 없음 (v1-β 결정)

선택한 LOD 의 slot 이 없으면 fallback 으로 다른 LOD 를 *그대로* 쓴다.
한 brick 의 LOD 를 마이그레이션하지 않음 → 시각 안정성 vs stale-LOD blur
trade-off. 정직히 기록.

### 7.5 Memory-budget atlas auto-sizing

기존엔 atlas grid 를 사용자가 명시. v1-α 가 자동화:

1. 시작점 `ceil(cbrt(nonEmptyBricks))`.
2. 512 MB budget 안에서 longest axis 부터 shrink.
3. 결과 = 1024³ default 280 → 188 MB (-33%).

`nonEmpty > slots` 이면 자동으로 Streaming 모드 진입.

---

## 8. Path Tracing — 시네마틱 품질 (M4)

레이마칭은 단일 산란 근사. 진짜 의료 시네마틱 (multi-scatter + soft shadow +
환경광) 은 path tracing 이 필요.

### 8.1 Free-path sampling (Woodcock tracking)

heterogeneous 볼륨에서 광선이 *실제로 어디서* 멈추는지 unbiased 샘플:

```wgsl
let sigmaMax = extinction * densityScale + epsilon;  // homogeneous majorant
var t = 0.0;
loop {
    t += -log(1 - rnd()) / sigmaMax;       // exponential sample
    if (t >= remaining) { exited = true; break; }
    let sigma_t = sampleDensity(uvw) * extinction;
    if (rnd() < sigma_t / sigmaMax) { break; } // 실제 산란
    // 아니면 null collision, 계속
}
```

핵심: majorant 로 가정 → 도착점 실제 `sigma_t / sigmaMax` 확률로 산란 결정.
unbiased.

### 8.2 위상함수 (Henyey-Greenstein)

산란 후 새 방향:

```text
p_HG(cosθ; g) = (1 - g²) / (4π × (1 + g² - 2g cosθ)^{1.5})
```

`g ∈ [-0.9, +0.9]` UI 슬라이더. 양수 = forward (생물 조직 일반), 음수 =
backward (드뭄).

inverse CDF 로 방향 샘플 (`sampleHG()`):

```wgsl
let sqr = (1 - g²) / (1 - g + 2g × u1);
cosθ = (1 + g² - sqr²) / (2g);
```

### 8.3 NEE — Next Event Estimation

산란점에서 직접 광원으로 가는 transmittance 계산 → 빛이 도달할 확률.
NEE 없으면 광원이 작거나 멀면 영원히 검정.

```wgsl
let Tl = transmittance(p, L, ...);   // 0 또는 1 (Woodcock)
let phase = hgPhase(dot(-rd, L), g);
result += throughput * albedo * lightI * phase * Tl;
```

### 8.4 Multi-bounce + Russian roulette

```wgsl
for b in 0..maxBounce {
    free-path → 산란점 또는 exit
    if exit: break (또는 P1 의 env 기여)
    NEE 광원
    sample HG → 새 방향
    throughput *= albedo
    if rnd() > rrProb: break    // RR 종료
    throughput /= rrProb         // 보정
}
```

기본 maxBounce=2, SPP=4. SPP 늘리면 frame 당 수렴 빠름 (선형 비용 증가).

### 8.5 Progressive accumulation (v1, 2026-06-02)

카메라 정지 시 매 프레임 결과를 running mean:

```wgsl
let blended = (prev * N + current) / (N + 1);
```

ping-pong RGBA16Float 텍스처 2 슬롯. reset 트리거: 카메라/window/preset/
SPP/anisotropy/bounces/모드/리사이즈 변경. N=0 reset 시 `prev × 0 = 0` →
clear 불필요.

별도 **display pass** 가 누적 → Reinhard tonemap → swapchain.

### 8.6 Environment lighting (v2 P1, 2026-06-17, `521a3f8`)

miss-ray 가 환경 그래디언트 sky 를 sample:

```wgsl
fn sampleEnvironment(dir) -> vec3<f32> {
    if (envBot.w < 0.5) { return vec3(0.0); }
    let t = clamp(dir.y * 0.5 + 0.5, 0, 1);
    return mix(envBot.rgb, envTop.rgb, t) * envTop.w;
}
```

두 호출 지점:

- Primary miss (광선이 처음부터 볼륨 안 만남) → 배경.
- Bounce escape (산란 후 free-path 가 매질 탈출) → IBL throughput contribution.

기본 활성, viewer 가 cool-top/warm-bot palette 설정.

### 8.7 A-trous spatial denoiser (v2 P2.2, 2026-06-17, `74b2473`)

path-trace 와 display 사이 third fragment pipeline.

5×5 cross-bilateral kernel:

```text
w_K = (1, 4, 6, 4, 1) outer product / 256   (binomial)
w_C = exp(-|c_n - c_c|² / σ_C²)              (color edge guide, σ_C=0.35)
denoised = Σ neighbor × (w_K × w_C) / Σ (w_K × w_C)
```

stride=4 → 25 tap 이 ~32 px 범위 cover. 의도적으로 gentle (progressive
accumulation 보완).

남은 작업:

- **P2.3** — multi-iteration cascade (stride 1/2/4 ping-pong).
- **P3** — adaptive SPP + temporal reprojection + accumulation N cap.

### 8.8 v2 파이프라인 흐름 도식

```text
Pass 1 (PathTrace fragment)  → accumTextures[1 - pp]
            ↓
Pass 1b (Denoise fragment)   → denoiseTexture   (denoise on 시만)
            ↓
Pass 2 (Display fragment)    → swapchain        (Reinhard tonemap)
   reads denoiseTex or accumTex[1-pp] (라우팅: getDisplayBindGroup())
```

호출 측은 단순 if (denoise) 분기. `VolumeRenderer` 가 source 자동 라우팅.

---

## 9. 데이터 입력 — NIfTI · DICOM

### 9.1 NIfTI (.nii)

신경영상학 단일 파일 포맷. 헤더 (352 B) + raw 본체. Mini-Engine 은
`utils::MmappedFile` 로 본문을 OS 페이지 캐시에 위임.

`src/assets/NiftiFile.cpp`:

- `loadNifti(path, out)` — 전체 로드, `Volume3D` 채움.
- `mmapNifti(path)` — `MmappedNiftiSource` (raw min/max, slope/intercept,
  mmap handle 묶음) 반환. brick-pack 시점에 변환 (disk paging Step 5).

### 9.2 DICOM (디렉토리 = 한 시리즈)

임상 표준. 디렉토리에 슬라이스마다 .dcm 파일. `loadDicomSeries(dir, out)`:

1. 디렉토리 스캔, 각 .dcm 의 transfer syntax + slice 메타 파싱.
2. `seriesInstanceUid` 별로 그룹.
3. `instanceNumber` 또는 `imagePositionPatient.z` 로 정렬.
4. 각 frame 디코드 → uncompressed int16 pixel layout 으로 통일.
5. `slope * raw + intercept` 로 HU 변환.
6. row flip (`a3a4133`) — DICOM row 0 = 환자 superior → world y=top.
7. `Volume3D{intensity, w, h, d, spacingX/Y/Z}` 채움.

지원 transfer syntax 9 종 (libjpeg-turbo + OpenJPEG + RLE):

| UID | 이름 | 디코더 |
| --- | --- | --- |
| `.2.1` | Explicit VR LE | 내부 |
| `.2` | Implicit VR LE | 내부 + VR dict |
| `.2.5` | RLE Lossless | 내부 PackBits |
| `.2.4.50` | JPEG Baseline | libjpeg-turbo (8-bit) |
| `.2.4.51` | JPEG Extended | libjpeg-turbo (12-bit) |
| `.2.4.57` | JPEG Lossless P14 | libjpeg-turbo |
| `.2.4.70` | JPEG Lossless SV1 | libjpeg-turbo |
| `.2.4.90` | JPEG 2000 Lossless | OpenJPEG |
| `.2.4.91` | JPEG 2000 Lossy | OpenJPEG |

NIfTI/DICOM 둘 다 `Volume3D` 공용 구조체로 결과 → 이후 엔진 경로 동일.

### 9.3 HU (Hounsfield Unit) — CT 의 의미 단위

```text
-1000 = 공기
0     = 물
+1000 = 빽빽한 뼈
+3000 = 금속 임플란트
```

DICOM 의 raw pixel 은 int16 (unsigned 가능). `slope * raw + intercept` 가
HU 변환 식. CT 보통 slope=1, intercept=-1024.

PixelRepresentation (0028,0103):

- 0 = unsigned int
- 1 = two's complement signed

`Slice::signedPixels` 가 이 비트 보존. 잘못 해석하면 음수 HU 가 64xxx 같은
거대 양수로 → empty-brick 검출 실패.

### 9.4 Multi-frame DICOM

NumberOfFrames > 1 인 단일 파일이 N slice 모두 들고 있는 경우. parser 가
하나의 frame 을 z slice 로 펴 z 축 dimension 결정.

### 9.5 Encapsulated PixelData (압축 transfer syntax)

표준 `(7FE0,0010) OB length=N` 대신:

```text
(7FE0,0010) OB length=0xFFFFFFFF
  (FFFE,E000) Item: BOT (Basic Offset Table, 길이 0 가능)
  (FFFE,E000) Item: frame 0 fragment 1
  (FFFE,E000) Item: frame 0 fragment 2   ← multi-fragment 가능
  ...
  (FFFE,E0DD) Sequence Delimitation
```

`walkEncapsulatedPixelData` 가 item 스트림 → `EncapsulatedFrame` 벡터.
single-frame multi-fragment 의 경우 `mergedFrameBuffer` 에 concat
(`6157da4` Step J3).

---

## 10. 디스크 페이징 — 데이터가 RAM 보다 클 때

### 10.1 mmap-backed source

`utils::MmappedFile` 가 NIfTI 본문을 OS 페이지 캐시에 위임. 데이터를
이중 복사 안 함 → working set 절감.

```cpp
struct MmappedNiftiSource {
    utils::MmappedFile mmap;
    size_t dataOffset;
    bool isSigned;
    uint16_t rawMin;
    float dataMin, dataMax;
    float slope, intercept;
    uint32_t w, h, d;
    float spacingX, spacingY, spacingZ;
};
```

### 10.2 `m_originalHalfData` 제거 (Step 5)

기존엔 `Volume3D float[w*h*d]` → `halfData uint16[w*h*d]` → `m_originalHalfData`
사본까지 동시 상주. 1024³ peak 6.57 GB.

해법: `VoxelSource` (HalfFloat / Int16 / Uint16) 추상화 + `buildFromMmappedSource`
가 brick-pack 시점에 *직접 변환*. 중간 버퍼 모두 제거.

### 10.3 결과

| 측정 | 이전 | Step 5 | 변화 |
| --- | --- | --- | --- |
| 1024³ dense peak working set | 6.57 GB | 2.30 GB | **-65%** |
| 정착 working set | 2.69 GB | 2.38 GB | -11% |

16 GB RAM 시스템에서 ~8 GB 임상 데이터 가능성 확보. 베이스라인:
`baselines/BASELINE_2026-06-10_DISK_PAGING_STEP5.md`.

---

## 11. 코드베이스 읽기 순서

### 첫 진입점

1. `docs/current/medical-volume/VIEWERS.md` — 사용자 가이드, 컨트롤, 셰이더 매핑.
2. `tests/volume_viewer_wasm.cpp` — 뷰어 main loop, 가장 쉬운 진입점.
3. `src/rendering/VolumeRenderer.hpp` — UBO 구조, public API.

### 핵심 모듈

| 모듈 | 핵심 파일 | 역할 |
| --- | --- | --- |
| 볼륨 데이터 IO | `assets/NiftiFile.cpp`, `assets/DicomFile.cpp` | NIfTI/DICOM 로드 |
| Bricked storage | `rendering/BrickedVolume.cpp` | atlas + page table + streaming |
| Volume render | `rendering/VolumeRenderer.cpp` | 마칭/path-trace/display 파이프라인 |
| 셰이더 | `shaders/volume_*.{wgsl,glsl}` | 마칭, path-trace, denoise, occupancy, display |

### 셰이더 매핑

| 기능 | GLSL (Vulkan) | WGSL (WebGPU) |
| --- | --- | --- |
| 레이마칭 | `volume_march.frag.glsl` | `volume_march.wgsl` |
| Path tracer | `volume_pathtrace.frag.glsl` | `volume_pathtrace.wgsl` |
| PT denoise | `volume_pathtrace_denoise.frag.glsl` | `volume_pathtrace_denoise.wgsl` |
| PT display | `volume_pathtrace_display.frag.glsl` | `volume_pathtrace_display.wgsl` |
| Occupancy compute | `volume_occupancy.comp.glsl` | `volume_occupancy.comp.wgsl` |

---

## 12. 더 읽을거리

### 학술 / 책

- Engel et al., *Real-Time Volume Graphics* (2006). Direct volume rendering
  교과서. window/level, gradient shading, ray marching 정석.
- Kulla, Fajardo, *Importance Sampling Techniques for Path Tracing in
  Participating Media* (2012). Woodcock, NEE 정석.
- Henyey, Greenstein, *Diffuse Radiation in the Galaxy* (1941). 위상함수 원전.
- Disney's *Cinematic Volume Rendering* talks (Siggraph 2017+). 의료
  cinematic rendering 의 산업 표준.
- Dammertz et al., *Edge-Avoiding A-Trous Wavelet Transform for fast Global
  Illumination Filtering* (2010). A-trous denoiser 원전.
- Schied et al., *Spatiotemporal Variance-Guided Filtering* (SVGF, 2017).
  덴 노이저 최신 형태, P3 트랙 참고.

### Mini-Engine 트랙 문서

- `docs/current/medical-volume/MEDICAL_VOLUME_ROADMAP.md` — 전략·진척.
- `docs/current/medical-volume/plans/*_PLAN.md` — 트랙별 계획서.
- `docs/current/medical-volume/baselines/*.md` — 마일스톤별 정량 측정.
- `docs/learning/COMPREHENSIVE_LEARNING_GUIDE.md` §9~16 — 같은 주제의 더
  깊은 이론 설명.

### 코드 참조

- `shaders/volume_pathtrace.wgsl` — Woodcock + HG + NEE + RR + env 흐름 한 곳에.
- `shaders/volume_pathtrace_denoise.wgsl` — A-trous 25-tap 한 함수에.
- `src/rendering/VolumeRenderer.cpp` — UBO, TF LUT, 파이프라인 lifecycle.
- `src/rendering/BrickedVolume.cpp` — page table, atlas, LRU streaming.
- `src/assets/DicomFile.cpp` — DICOM 파서 + 9 종 transfer syntax dispatch.

---

## 부록: 마일스톤별 진척 요약

| 마일스톤 | 상태 | 핵심 |
| --- | --- | --- |
| M1 | ✅ | R16Float + window/level + NIfTI + DICOM 9 종 |
| M2 | ✅ | gradient shading + volumetric soft shadow |
| M3 | ✅ | empty-space skip + brick atlas + multi-LOD + LRU streaming + disk paging |
| M4 v0 | ✅ | Woodcock + HG + NEE + inline SPP |
| M4 v1 | ✅ | progressive temporal accumulation (ping-pong) |
| M4 v2 P1 | ✅ | environment lighting (miss-ray IBL) |
| M4 v2 P2.1·P2.2 | ✅ | denoise plumbing + single-iter A-trous |
| M4 v2 P2.3 | 🔲 | multi-iteration cascade (stride 1/2/4) |
| M4 v2 P3 | 🔲 | adaptive SPP + temporal reprojection |
| M4 v2 P4 | 🔲 backlog | HDR equirect IBL |

진행 추적: `docs/current/medical-volume/plans/PATH_TRACE_POLISH_PLAN.md`.
