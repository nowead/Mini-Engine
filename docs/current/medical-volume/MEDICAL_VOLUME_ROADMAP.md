# Mini-Engine — 의료 볼륨 렌더링 방향 계획서

**작성일**: 2026-05-29 (원본) · **재정비**: 2026-07-10
**관점 (재정비 후)**: **저사양 PC · 모바일 · 태블릿 브라우저 (WebGPU + WASM)
에서 대용량 실 CT/MRI 를 유의미한 fps/메모리로 렌더링** — 접근성 축의 성능
차별화. 원 목표 원문 및 스코프 경계는 §5 말미
[스코프 재정비 (2026-07-10)](#스코프-재정비-2026-07-10--원-목표-lens-재정렬) 참조.
**네이티브 Vulkan 은 개발·검증·프로파일링 1차 백엔드로 dual-backend parity 유지**.

**Mini-Engine 스코프 경계**: 렌더링/그래픽 엔진. 임상 워크플로우 도구
(MPR · 측정 · 워크리스트 · 주석) 는 **스코프 밖** — 필요하면 상위 앱
프로젝트로 분리.

---

## 0. 전략 결정 (2026-05-29)

세 가지 사업 방향(WebGPU 시네마틱 SaaS / AI 마스크 하이브리드 / 수술실 XR
스트리밍)을 현재 자산과 "WebGPU 차별화" 제약에 비춰 평가한 결과 **방향 1**을
선택했다.

| 방향 | WebGPU 중심성 | 외부 의존 | 단독 시연 | 판정 |
| --- | --- | --- | --- | --- |
| 1. WebGPU 시네마틱 뷰어 | **핵심** | 없음 | 가능 | **채택** |
| 2. AI 마스크 하이브리드 | 보조 | 외부 세그멘테이션 모델/파트너 | 어려움 | 후속 후보 |
| 3. 수술실 XR 스트리밍 | 낮음(Vulkan 서버+NVENC) | XR HW·인코더·아이트래킹 | 불가 | 보류 |

**차별화 명제 (정직하게)**:

- 단순 레이마칭은 WebGL2로도 된다 — 여기서 차별화는 없다.
- 진짜 해자는 **(a) 컴퓨트 기반 대용량 데이터 처리(empty-space skipping →
  bricking → sparse), (b) 컴퓨트 기반 시네마틱 품질(볼류메트릭 섀도우 →
  path-traced 산란)**. 둘 다 WebGL의 컴퓨트·메모리 제약으로 못 하는 영역이고
  WebGPU가 비로소 여는 지점이다.
- 즉 "브라우저에서, 서버 GPU 없이, 시네마틱 화질로, 대용량 실데이터를" — 이
  네 가지가 동시에 성립하는 데모가 시장 가치의 핵심.

**이중 백엔드 패리티 원칙 (사용자 요구)**: 모든 마일스톤은 Vulkan + WebGPU
양쪽에서 동작해야 한다. 포맷·기능은 두 백엔드의 교집합(lowest common
denominator)을 기준으로 설계한다. 대표 제약:

- 텍스처 포맷은 코어 필터러블 교집합 사용 — **R16Float**(둘 다 코어·필터러블).
  16비트 unorm(`r16unorm`)은 WebGPU 코어가 아니므로 금지.
- WebGPU에는 bindless 없음 — 머티리얼/브릭 인덱싱은 per-group 바인딩으로.
- WebGPU `copyBufferToTexture`의 `bytesPerRow`는 256 정렬 필요(CLAUDE.md §9).
- 컴퓨트 셰이더는 양쪽 모두 가용(이미 frustum cull·IBL bake에서 사용 중).

---

## 1. 현재 위치 — 볼륨 렌더러가 가진 것 vs 의료 실사용까지의 격차

### 이미 있는 것 (commit `fc2fb01`까지)

| 항목 | 상태 |
| --- | --- |
| 3D 텍스처(R8Unorm) + 프래그먼트 레이마칭 | ✅ Vulkan + WebGPU |
| Beer-Lambert front-to-back, premultiplied over 블렌드 | ✅ |
| Transfer Function LUT(256×1 RGBA8) + 프리셋 5종 | ✅ |
| depth-aware occlusion(씬 합성) / off(독립 뷰어) | ✅ |
| 엔진/앱 분리 seam(`loadFromData`) + raw 로더(app layer) | ✅ |
| 독립 실행 뷰어(`volume_viewer`, ImGui, orbit, raw 로드) | ✅ |
| 브라우저 HTML 컨트롤(밀도/색/프리셋) | ✅ |

### 의료 실사용까지의 격차

| 격차 | 왜 막혀 있나 | 해결 마일스톤 |
| --- | --- | --- |
| **8비트로 HU 손실** | R8Unorm + uint16 min/max 압축 → Hounsfield 정밀도·윈도잉 능력 소멸 | M1 |
| **window/level 없음** | 임상은 같은 볼륨을 bone/lung/soft-tissue 윈도우로 전환해 본다 | M1 |
| **실데이터 입력 없음** | raw headerless만 — 실제 데이터는 DICOM/NIfTI | M1 |
| **물리 단위 무시(등방 가정)** | CT는 z-spacing ≠ xy-spacing → 등방 박스로 띄우면 찌그러짐 | M1 |
| **음영 없음(흡수만)** | gradient 셰이딩·조명 없음 → 입체감 약함 | M2 |
| **시네마틱 품질 없음** | 볼류메트릭 섀도우·산란 없음(임상이 원하는 VRT 입체감) | M2/M4 |
| **대용량 불가** | 전부 단일 3D 텍스처 — VRAM 초과 볼륨 못 띄움 | M3 |

---

## 2. 마일스톤 (M1 → M4)

각 마일스톤은 **독립적으로 데모 가능**하도록 자른다. 모든 항목은 Vulkan에서
먼저, WebGPU 포팅으로 닫는다.

### M1 — 실데이터 기반: 16비트 HU + window/level + 실파일 로더

**목표**: 실제 CT/MRI 한 볼륨을 올바른 물리 비율로, 임상 윈도우를 바꿔 가며
네이티브 뷰어와 브라우저 양쪽에서 본다.

**상태 (2026-05-29)**: 핵심 3단계 완료. ~~M1-1 R16Float~~ ✅ (`3e6a2e8`) →
~~M1-2 window/level~~ ✅ (`3e6a2e8`) → ~~M1-3 NIfTI 로더 + float 강도 경로 +
임상 윈도우 프리셋~~ ✅ (`d4f9659`, 합성 .nii로 검증). **잔여 M1 후속**:
(a) 브라우저 실 CT 표시(WASM .nii preload — 현재 브라우저는 절차적 볼륨만),
(b) DICOM 로더. 둘 다 별도 작업으로 분리.

**작업**:

- **M1-1 — 3D 텍스처 R8Unorm → R16Float 승격.** 정규화 강도(또는 HU)를
  16비트 float로 저장. 업로드 경로의 voxel당 바이트=2로 일반화(`uploadVolume`의
  per-row 패딩·스테이징 크기·WebGPU 256정렬이 width→width×2로 바뀜). 셰이더는
  이미 `.r`을 float로 읽으므로 샘플링 로직 무변경. 기존 테스트 볼륨으로 양
  백엔드 무회귀 확인.
- **M1-2 — window/level transfer function.** UBO에 `windowCenter`,
  `windowWidth` 추가. 셰이더에서 TF 적용 전 `n = clamp((v - (C - W/2)) / W, 0, 1)`
  로 정규화. 임상 프리셋 추가(bone C=300/W=1500, lung C=-600/W=1500,
  soft-tissue C=40/W=400, brain C=40/W=80). 뷰어 ImGui + 브라우저 컨트롤에
  슬라이더·프리셋 노출.
- **M1-3 — 실파일 로더(app layer, 네이티브).** 강도 + 물리 spacing을 읽어
  R16Float 볼륨 + 물리 비율 AABB 구성.
  - **NIfTI 우선**(단일 파일, 단순 헤더, 공개 CT/MRI 데이터 풍부) →
  - **DICOM 후속**(임상 표준; 멀티 슬라이스 시리즈, rescale slope/intercept→HU,
    Explicit/Implicit VR LE 우선, 압축 변형은 후속). *포맷 우선순위는 §5에서
    사용자 확정 필요.*

**이중 백엔드 메모**: R16Float는 양쪽 코어·필터러블. WebGPU 업로드 256정렬을
2바이트 기준으로 재계산.

**데모**: 실제 흉부 CT를 bone↔lung↔soft-tissue 윈도우로 전환. 네이티브 +
브라우저 동일 결과.

**리스크**: 중. DICOM 파싱 코너 케이스(VR·transfer syntax·멀티프레임)가 깊다 →
NIfTI로 실데이터를 먼저 띄워 파이프라인을 검증한 뒤 DICOM 확장.

### M2 — 시네마틱 품질: gradient 셰이딩 → 볼류메트릭 소프트 섀도우

**목표**: 흡수만 하던 볼륨에 빛과 그림자를 넣어 입체감을 만든다. "와우"
포인트.

**상태 (2026-05-29)**: 핵심 완료. ~~M2-1 gradient 셰이딩~~ ✅ (`a37b95b`) →
~~M2-2 볼류메트릭 소프트 섀도우~~ ✅ (`32c988f`). 양 백엔드 + 네이티브/브라우저
토글. **M2-3(라이트 누적 최적화)은 보류** — 현재 brute-force 보조 레이로 충분
(작은 볼륨). 성능이 문제되면 M3의 empty-space skipping과 함께 재검토.

**작업**:

- **M2-1 — gradient 셰이딩.** 중앙차분으로 밀도 gradient(=법선) 계산 →
  Lambert/Blinn-Phong 음영. 표면 형태 인지 즉시 향상. (WebGL도 가능한
  table-stakes — 여기까진 차별화 아님, 품질 기반.)
- **M2-2 — 단일 광원 볼류메트릭 소프트 섀도우.** 샘플마다 광원 방향 보조
  레이로 누적 투과율(self-shadowing). 비용이 크므로 step·해상도 분리 + 조기
  종료. 이 지점부터가 WebGL이 어려운 영역.
- **M2-3 — half-angle slicing 또는 라이트 누적 최적화**(선택). 매 샘플 보조
  레이가 너무 비싸면 광원 방향 누적 패스를 분리.

**이중 백엔드 메모**: 보조 레이 루프는 양쪽 프래그먼트에서 동일. WGSL의
non-uniform control flow는 `textureSampleLevel`로(현 셰이더 관례 유지).

**데모**: 같은 CT를 음영 on/off A/B. 광원 회전 시 장기/뼈의 입체 그림자.

**리스크**: 중. 보조 레이로 프레임 시간 급증 → M3의 empty-space skipping과
시너지(둘을 같이 보면 비용이 상쇄).

### M3 — 스케일 & 성능: empty-space skipping → bricking → (sparse)

**목표**: "브라우저에서 대용량 실데이터를 60fps로" 주장의 근거. WebGPU 컴퓨트가
차별화를 만드는 핵심 마일스톤.

**상태 (2026-05-31)**: ~~M3-1 컴퓨트 occupancy 그리드 + 마칭 스킵~~ ✅ (`b6fbaed`,
양 백엔드, 셀 8³, storage buffer). **M3-2 측정 결과**:

- 합성 sparse CT 256³(99% 공기), 기본 줌(구조 작게): skip ON 600 / OFF 580 — +3%
- 합성 sparse CT 256³, 줌인(구조 화면 채움): skip ON 112 / OFF 107 — +5%
- 합성 dense 256³(60% 공기), 줌인: skip ON 107 / OFF 102 — +5%

이 워크로드(Lambert+shadow 셰이딩 우세, 베이스라인 빠름)에서 occupancy의 절감
여지는 본질적으로 좁음. 시도한 **per-cell-entry 최적화는 GPU에서 warp divergence로
회귀**(CPU 직관과 반대) — 원래 per-sample 체크는 cache가 read를 무료화해 더 빠름.

**부수 산출물(M3-2 작업 중 발견)**:

- VMA staging pool oversize 픽스 (16MB 블록 한도 → 32MB 256³ 업로드 OOM 해소).
- `make_synthetic_nii.py`에 구조 크기 인자 추가(sparse CT 시뮬레이션).

**남은 헤드라인 ("1GB 60fps")**: 진짜 실 CT(DICOM) + 더 큰 볼륨에서 측정해야
의미 — 현재 작은 합성에선 fixed overhead가 frame time의 대부분. **M1 후속 DICOM
로더가 다음 단계로 자연스러움**. M3-3(bricking)은 그 뒤 재판단.

**작업**:

- **M3-1 — min/max occupancy 그리드(컴퓨트 빌드).** 볼륨을 매크로셀(예 8³)로
  나눠 셀별 min/max 강도를 컴퓨트로 사전 계산. 현재 window/level에서 보일 게
  없는 셀(공기 등)을 마칭에서 건너뜀. 의료 데이터는 공기 비중이 커서 효과 큼.
- **M3-2 — 성능 측정.** GPU 타이머로 skipping on/off 프레임 시간. 목표:
  대표 볼륨(예 512³ 또는 실 CT ~512×512×N)에서 60fps.
- **M3-3 — bricking / multi-resolution(스트레치).** VRAM 초과 볼륨을 브릭으로
  분할, resident 브릭만 페이징. "1GB+ 브라우저" 주장의 본체. SVO/NanoVDB류
  희소 레이아웃은 더 깊은 단계 — 여기서 범위 재판단.

**이중 백엔드 메모**: 컴퓨트는 양쪽 가용하나 WebGPU엔 bindless 없음 → 브릭
인덱싱은 per-group 바인딩/간접 테이블로. occupancy 그리드는 별도 3D 텍스처
또는 storage buffer.

**데모**: skipping on/off 토글 + 프레임 타이머. 큰 볼륨 부드러운 회전.

**리스크**: 상. bricking·스트리밍은 작업량이 크고 양 백엔드 메모리 모델 차이가
큼. M3-1/M3-2(skipping + 측정)까지가 핵심, M3-3은 별도 결정.

### M4 — Path-traced 산란 (스트레치, 진짜 시네마틱 VRT)

**목표**: 임상 워크스테이션급 시네마틱 렌더링(다중 산란·소프트 GI)을 브라우저
컴퓨트로. 가장 강한 차별화.

**작업**:

- 컴퓨트 기반 볼류메트릭 path tracing(delta/Woodcock tracking), 카메라 정지 시
  프레임 누적(progressive). 다중 산란 + 소프트 섀도우 + 환경광.
- TAA 인프라(이미 보유)와 누적 버퍼 재사용 검토.

**리스크**: 상. 수렴·노이즈·성능 난제. M1~M3가 단단해진 뒤 착수.

---

## 3. 첫 작업 — M1-1 (R16Float 승격) 구체화

가장 작고 즉시 빌드·검증 가능한 진입점. 시각 결과는 동일(무회귀)하되 이후
모든 작업의 토대.

**변경 면적**:

- `VolumeRenderer::uploadVolume` — voxel당 바이트를 1→2로 일반화.
  `tightBytesPerRow = w * 2`, WebGPU 패딩/스테이징/`bytesPerRow`를 그에 맞게.
  텍스처 포맷 `R8Unorm → R16Float`. 입력 버퍼를 float16으로 변환(또는 R16Float에
  맞게 패킹)하는 단계 추가.
- `loadFromData` / `VolumeFile` — 8비트 정규화 대신 16비트 강도 전달 경로.
  단, M1-1은 *포맷 승격*만 — 기존 테스트 볼륨(uint8)을 16비트로 올려 동일 결과
  확인까지. 실데이터(M1-3)는 분리.
- 양 백엔드 빌드 + 기존 `volume_test.raw` / 쇼케이스 구름으로 무회귀 확인
  (검증 레이어 0, 시각 동일).

**완료 기준**: 네이티브 뷰어·쇼케이스·브라우저 모두 기존과 동일하게 렌더,
검증 에러 0. 그 위에서 M1-2(window/level)가 16비트 강도를 받아 동작.

---

## 4. 측정 & 차별화 정직성

CAREER_ROADMAP의 "수치화가 전부다" 원칙 계승. 마일스톤별 정량 지표:

| 마일스톤 | 지표 |
| --- | --- |
| M1 | 실 CT 1볼륨 로드·렌더, 윈도우 전환 3종, 물리 비율 정확 |
| M2 | 음영 on/off GPU 시간 차, 소프트 섀도우 보조 레이 비용 |
| M3 | empty-space skipping on/off 프레임 시간, 목표 볼륨 60fps |
| M4 | path-traced 수렴 프레임 수, 정지 시 노이즈 감소 곡선 |

**정직성 메모**: M1·M2-1은 WebGL로도 가능한 기반이다. 마케팅·포트폴리오에서
차별화로 내세울 부분은 **M2-2 이후(컴퓨트 섀도우)**, **M3(컴퓨트 occupancy·
대용량)**, **M4(브라우저 path tracing)** 이다. 이 경계를 흐리지 않는다.

---

## 5. 진행 상태 · 결정 기록 · 다음 진입점

**완료**:

- M1 (R16Float 16비트 · window/level · NIfTI 로더 + float 강도 + 임상 윈도우 프리셋)
- M2 (gradient 셰이딩 + 볼류메트릭 소프트 섀도우)
- M3-1 (컴퓨트 occupancy 그리드 + 마칭 empty-space skipping)
- 독립 WASM 볼륨 뷰어(`volume_viewer_wasm`) — 브라우저에서 볼륨만 풀스크린.
  네이티브 `volume_viewer`와 한 쌍. 랜딩 인덱스에서 클릭 진입.
- M3-2 측정 (합성 sparse 256³, skip +3~5%, 부수: VMA staging oversize 픽스 +
  생성기 구조 인자)
- **DICOM 로더** (Explicit VR LE 단일 시리즈, int16 CT/MR; viewer 디렉토리 dispatch).
  `Volume3D` 공용 구조체로 NIfTI/DICOM 동일 엔진 경로. 합성 DICOM 생성기 추가.
- **M4 v0 path-traced 산란** (Woodcock 자유경로 + Henyey-Greenstein 위상함수 +
  single-light NEE + inline SPP 평균). 두 번째 fragment 파이프라인을 같은 bind group
  layout으로 추가. **누적 버퍼는 v1로 보류** — 매 프레임 독립 샘플이라 노이즈 항상
  보임, 정지 카메라 progressive convergence는 다음.
- **실 임상 DICOM 검증** (2026-06-01): pydicom 공개 코퍼스 4종(HU CT, 고해상도 MR,
  multi-frame MR, enhanced CT) 모두 로드. 발견·수정한 실세계 갭 2개 — NumberOfFrames
  지원, 중첩 undefined-length sequence skip(item-walk). 로더 외 엔진 무변경.
  M3-2 헤드라인은 미해결(공개 코퍼스에 큰 multi-slice CT 시리즈 부재; TCIA류 인증
  필요한 데이터셋에서 재측정).
- **M4 v1 progressive 누적** (2026-06-02): 정지 카메라에서 매 프레임 path-trace 결과를
  RGBA16Float ping-pong 텍스처에 running-mean으로 누적, 두 번째 패스가 tonemap만
  담당. 카메라/윈도우/프리셋/SPP/anisotropy/bounces/모드/리사이즈 변경 시 N=0
  reset (N=0이면 history×0이라 텍스처 clear 불필요). path-trace는 자체 bind-group
  layout — depth/occupancy 제거, history 추가. WebGPU `textureLoad`는 sampler 미사용,
  GLSL `texelFetch`는 sampler 필요 → display 바인딩이 백엔드별로 갈림. 네이티브
  뷰어는 카메라+파라미터 비교로 reset, WASM 뷰어는 JS-bound setter마다 reset 호출
  및 카메라 매트릭스 비교(JS 훅 없음).
- **M3-3 v1-β LOD multi-resolution** (2026-06-07): mip chain CPU build →
  per-LOD atlas allocation → distance-based per-brick LOD selection →
  multi-LOD streaming(no migration + LOD fallback when chosen LOD full +
  K=8→64 upload budget) → 셰이더(`sampleVolume`)가 page table에 인코딩된
  `(lod<<30)|slot`을 디코드해서 4 LOD atlas 중 선택된 것을 샘플링. Case C
  (1024³ dense): missing brick 2320 → 326 (~21% hole, -86%) ; atlas memory
  L0 단독 493.5 MB → 4 LOD 합 ~572 MB (+16%, 1.16× ratio from 1/8+1/64+1/512).
  알려진 한계: LOD 경계 seam(인접 brick LOD 다를 때 sampling 불연속), no-migration
  결정으로 stale-LOD blur 일부 (시각 안정성 trade-off). 베이스라인:
  [BASELINE_2026-06-07_V1_BETA.md](baselines/BASELINE_2026-06-07_V1_BETA.md).
- **M3-3 v1-α streaming** (2026-06-04): LRU + incremental upload + memory-budget
  auto-size. Atlas 자동 산정 재작성 — `ceil(cbrt(nonEmpty))` 시작점에서 pageGrid
  clamp + 512MB budget 안에 longest axis shrink. nonEmpty > slots면 자동 Streaming
  진입, 매 프레임 K=8 brick CPU pack → `copyBufferToTexture`. LRU eviction은
  visible bumped slot 보호 (churn 방지). Streaming 진입 시 LOG_WARN으로 권장
  atlasGrid + "visible-set << atlas 가정" 명시. 1024³ default: atlas 280→188 MB
  (-33%), Static 유지. 2 GB dense 자동 streaming(493 MB). 한계: 가시 brick >
  atlas slots면 시각 hole (v1-β LOD가 본질 해결).
- **M3-3 v0 bricked storage** (2026-06-02): 단일 dense 3D 텍스처 → brick atlas
  (64³ interior + 1-voxel halo = 66³ stored) + page table(storage buffer of uint32
  slot indices, 0xFFFFFFFF = "air") 간접 참조로 교체. 모든 density read가 셰이더
  helper `sampleVolume(uvw)` 거쳐 page-lookup → linear atlas 샘플로 진행 (3 셰이더
  세트: march · pathtrace · occupancy — 양 백엔드). 빈 brick(interior 전체 = 최솟값)은
  atlas에 안 올라가 → 1024³ × 10% 점유 시 2GB → ~200MB. v0는 로드 타임 단발 packing,
  streaming/LRU는 v1로 연기. atlas 초과 시 build() 실패 반환. 기본 atlas (4,4,4)
  = 64 슬롯 (37MB 네이티브 / 53MB WebGPU). 부수로 GLSL march UBO에 누락돼 있던
  `pathtrace` 필드 발견·정렬(이전엔 occ 뒤 바로 accum이라 read하면 silently 다른
  값 읽힘 — march는 그 슬롯을 read하지 않아 노출 안 됨).

**결정 기록**:

- *실파일 포맷*: **NIfTI 우선으로 결정·완료**(`d4f9659`). DICOM은 다음 단계로.
- *WASM 데모 분리*: **별도 실행파일로 결정·완료**(`7089d84`). 쇼케이스에 묻히던
  볼륨 가시성 문제 해소. (디버깅: ASYNCIFY 재진입 가드 + DOM 핸들러의 직접 wasm
  호출 제거 — 데이터 범위를 `Module._dataMin/Max`로 push.)
- *Skip 일반화 시도*: **per-cell-entry 변형은 GPU warp divergence로 회귀 → 되돌림**
  (2026-05-31). CPU 직관과 반대 — 원본 per-sample 체크는 cache가 read를 무료화해
  더 빠름. 셰이더 주석에 기록.

**다음 진입 작업**: 사용자 결정 — Step 5+ disk paging (mmap'd int16 → on-demand
brick conversion, 진짜 4 GB+ 지원) 또는 WASM openjpeg 빌드 또는 즉흥 폴리시.
v1-β LOD + DICOM (Implicit VR + 압축) + Disk paging Steps 1-3 까지가 한 마디.

**완료된 로드맵 후보**:

- **DICOM Implicit VR LE 지원** (`5ea4577`, `6df12a2`, `d328afe`) — 임상 PACS 기본
  transfer syntax. 합성 + 실 RT 파일 dispatch 검증.
- **DICOM 압축 transfer syntax** (`ce26dcb` → `5a2f8ce`, 4 step) — RLE Lossless
  (in-tree PackBits) + JPEG 2000 (openjpeg vcpkg). 비트 동일성 + lossy decode
  검증.
- **M3-3 v1-β 디스크 페이징 Step 1-3** (`32eb364` → `b9fda20`) — voxel source
  추상화 + NIfTI mmap + on-the-fly LOD 박스 필터. 1024³ 정착 working set
  -11%. 베이스라인: [BASELINE_2026-06-07_DISK_PAGING.md](baselines/BASELINE_2026-06-07_DISK_PAGING.md).
- **M3-3 v1-β 디스크 페이징 Step 5** (`1ff245d` → `df1710b`, 2026-06-10) —
  VoxelSource Int16/Uint16 + `buildFromMmappedSource` + NIfTI int16 mmap을
  brick-pack 시점에 직접 변환. 1024³ dense **피크 working set -65%
  (6.57 → 2.30 GB)**. m_originalHalfData + float intermediate + halfData 변환
  임시 버퍼 모두 제거. 16 GB RAM 시스템에서 ~8 GB 임상 데이터 가능성 확보
  (추정). 베이스라인: [BASELINE_2026-06-10_DISK_PAGING_STEP5.md](baselines/BASELINE_2026-06-10_DISK_PAGING_STEP5.md).
- **WASM OpenJPEG (브라우저 압축 DICOM)** (`90a4f38` → `f7f62a3`, 2026-06-10) —
  Emscripten 빌드에 OpenJPEG FetchContent + DicomFile.cpp 연결 + pydicom
  693_J2KI.dcm preload + 뷰어 dispatch. 브라우저에서 실 임상 CT(JPEG 2000 lossy)
  디코드 + 렌더 검증. WASM 738 KB → 1.08 MB (+345 KB OpenJPEG 정적 링크).
  부수 발견: WASM 뷰어의 Streaming 모드 + ASYNCIFY + WebGPU swapchain 상호작용
  으로 "Destroyed texture in submit" 검증 spam — Static atlas 강제 워크어라운드
  적용, 본질 해결은 후속 트랙. 계획서:
  [WASM_OPENJPEG_PLAN.md](plans/WASM_OPENJPEG_PLAN.md).
- **DICOM JPEG Baseline/Extended/Lossless P14·SV1 (네이티브)**
  (`6ff902c` → `6157da4`, 2026-06-16) — libjpeg-turbo 3.1.2 vcpkg 의존성
  추가, DicomFile.cpp 디코더 (`decodeJpegFrame16` precision 분기로 8/12/16-bit
  처리) + parseSlice dispatch + 단일-frame multi-fragment 병합. pydicom-data
  JPGExtended(12-bit DCT) `[0, 264]`, JPEG-LL(16-bit lossless, 2 fragments)
  `[0, 278]` 검증, JPEG 2000/RLE 무회귀. 계획서:
  [DICOM_JPEG_LEGACY_PLAN.md](plans/DICOM_JPEG_LEGACY_PLAN.md).
- **WASM libjpeg-turbo (브라우저 JPEG legacy)**
  (`9f83230` → `7b129fd`, 2026-06-17) — Emscripten 빌드에 libjpeg-turbo
  3.1.2 FetchContent + jpeg-static 정적 링크 + DicomFile.cpp 그대로 재사용.
  Upstream의 `add_subdirectory` 거부 (FATAL_ERROR) 우회: manual populate +
  `string(REPLACE)` 패치, `include(GNUInstallDirs)`, `uninstall` 타겟 rename
  (tinyobjloader와 이름 충돌 회피). preload 분리:
  `/sample_dicom_jp2/693_J2KI.dcm` (JPEG 2000 회귀) +
  `/sample_dicom_jpegll/JPEG-LL.dcm` (JPEG Lossless 16-bit). 브라우저
  콘솔에서 `libjpeg-turbo linked, JPEG_LIB_VERSION=62` + JPEG-LL `[0,278]`
  디코드 확인. WASM 1.08 MB → 1.41 MB (+327 KB, OpenJPEG와 비슷한 규모).
  계획서: [WASM_LIBJPEG_TURBO_PLAN.md](plans/WASM_LIBJPEG_TURBO_PLAN.md).
- **단일-슬라이스 DICOM 시각화 폴리시** (`a3a4133`, 2026-06-17) — JPEG-LL
  검증에서 드러난 4가지 기본 UX 문제 묶음 수정:
  (a) DicomFile.cpp row flip — DICOM row 0(상단)이 world y=+halfExtent.y
  (화면 상단) 으로 가도록, native/WASM 모든 transfer syntax 영향;
  (b) `Camera::setOrbit(yaw, pitch, distance)` 추가 — 절대 자세 설정 가능;
  (c) 조건부 HU 본 윈도우 — `dataMin < -500`(CT air) 만 300/1500 적용,
  비-CT는 auto-fit 유지; (d) 가장 얇은 halfExtent 최소 0.1 padding —
  단일 슬라이스가 ray-marching에서 0 alpha 안 되도록; (e) 비-CT는 Cloud
  preset (1) 디폴트; (f) `vol.d == 1` 면 face-on 카메라 (yaw=0, pitch=0,
  distance=2.3). 결과: JPEG-LL이 슬라이더 안 만지고 보임.
- **M4 v2 P1 path-trace IBL (환경광)** (`521a3f8`, 2026-06-17) —
  VolumeUBO에 envTop/envBot 두 vec4 추가, `sampleEnvironment(dir)`
  top-bottom 그래디언트 sky. `tracePath()` 양쪽 miss 분기에서 호출:
  primary-miss(배경) + bounce escape(멀티-scatter throughput에 IBL 기여).
  WGSL + GLSL 미러, native + WASM 디폴트 on. v0/v1의 검은 배경이 환경광
  그래디언트로 대체, multi-bounce가 진짜 IBL contribution 받음.
  계획서: [PATH_TRACE_POLISH_PLAN.md](plans/PATH_TRACE_POLISH_PLAN.md).
- **M4 v2 P2.1/P2.2 spatial denoiser (A-trous)** (`c34d85c` → `74b2473`,
  2026-06-17) — path-trace와 display 사이에 third fragment pipeline 삽입.
  P2.1 plumbing: `m_denoiseTexture`, `m_pathDenoisePipeline`,
  `m_pathDenoiseBindGroups[2]`, `getDisplayBindGroup()` routing.
  P2.2 5x5 cross-bilateral kernel — binomial (1,4,6,4,1) outer-product
  weights × color-similarity `exp(-|d|²/0.35²)`, stride=4 (~32px 커버).
  의도적으로 gentle — progressive temporal accumulation과 보완, 카메라
  이동 직후 grain만 부드럽게. UI 토글 + JS binding. 정지 상태에선 시각
  차이 미세(누적이 이미 수렴) — P3에서 누적 N cap 도입하면 가시화. 계획서:
  [PATH_TRACE_POLISH_PLAN.md](plans/PATH_TRACE_POLISH_PLAN.md).
- **M4 v2 P2.3 A-trous cascade + swapchain race fix** (`5f6723a`,
  2026-07-07) — 단일 iteration을 3-level cascade로 확장 (strides 1/2/4,
  ping-pong denoise 텍스처 2개). 각 iteration 별 16 B stride UBO로 단일
  pipeline이 세 pass 모두 구동. Iter 0은 accum ping-pong별 2 variant,
  iter 1/2는 fixed. SPP=1 + 카메라 드래그 시 P2.2의 미묘하던 차이가
  뚜렷해짐. 동시에 dangling swapchain 포인터 race 수정 —
  `RendererBridge::onResize()` 후 캐시된 raw pointer가 stale 되어 F12
  toggle 시 display pipeline이 R8Unorm target으로 재생성되며 crash. 두
  뷰어 모두 `m_swapchain = m_bridge->getSwapchain()` 재획득으로 방어.
- **M4 v2 P3.1 accumulation N cap** (`d50e76f`, 2026-07-07) — spatial
  denoise가 정지 상태에서도 상시 가시 contribution을 갖도록 temporal
  N 을 32에서 캡. `advanceAccumulationFrame()`이 denoise 활성 시에만
  clamp (off 시엔 uncapped, v1 그대로). `setDenoiseEnabled` transition
  시 자동 accumulation reset으로 정책 즉시 반영. WASM 셸에 accum HUD
  라인 + cap 슬라이더 + reset 버튼 추가. **M4 v2 P3의 진짜 목표
  ("spatial denoise stays visible at rest") 달성.** P3.2 (adaptive
  SPP) / P3.3 (SVGF temporal reprojection) 은 최적화·인프라 성격이라
  **명시적으로 유예** — real-MRI 검증 트랙이 원 프로젝트 목표에 더 직접
  기여함. 계획서:
  [PATH_TRACE_POLISH_PLAN.md](plans/PATH_TRACE_POLISH_PLAN.md).

---

### 실 MRI 검증 트랙 완료 (2026-07-07~2026-07-08)

- **R1 실 MR 번들 + preset 자동 선택 + UI sync** (`933faa5`) — pydicom-data
  emri_small (fMRI 64×64×10) preload + fallback chain 상단 배치;
  preset heuristic 확장 (CT/-500 · MR/0-4096 · else Cloud); JS ↔ wasm
  UI 값 동기화 (dropdown / window slider / subtitle).
- **R3 런타임 DICOM 업로드** (`91cf26a` → deferred-reload 후속) —
  `<input type="file" webkitdirectory>` + memfs write + wasm 측
  deferred-reload 패턴 (`queueUserDicomReload` → render() 시작에서 처리
  → `lastReloadStatus` 폴). 초기 즉시 호출 버전은 embind 가 wasm suspend
  시 즉시 undefined return → JS finally 가 busy flag 조기 해제 → 재진입
  crash. 원인 진단 후 M3-3 swapchain race 와 동일한 defer-to-render 패턴
  으로 근본 해결.
- **R4 FPS/메모리 HUD + baseline** (2026-07-08) — frame time ring buffer
  (mean / max, 500 ms 샘플, ~60 sample window), 업로드 latency
  breakdown (read / write / decode), 얇은 볼륨 memory overhead 표시
  버그 (--%%%) 수정. 4개 공개 시리즈 baseline 측정:
  [BASELINE_2026-07-08_REAL_MRI.md](baselines/BASELINE_2026-07-08_REAL_MRI.md).
  관측: fMRI 3D shape 은 memory overhead +602% 로 상대적으로 낮음, 단일
  슬라이스 (`d=1`) 는 atlas depth 낭비로 +6919~7754% 오버헤드 — bricking
  의 정직한 한계. CPU 는 dim 이 아니라 content-driven (fMRI 6.5 ms
  vs 매머그래피 2.4 ms).

**목표 달성**: 프로젝트의 원 목표 — "실제 MRI 를 유의미한 프레임과
메모리로 화면에 띄우기" — 대응 완료. 사용자가 자신의 DICOM 폴더를
브라우저에 드래그하면 즉시 렌더 되며, 실 fMRI 데이터에서 6.5 ms/frame
(=153 FPS 이론상, 실제 60 FPS 캡) 로 실시간 동작. 계획서:
[REAL_MRI_VERIFICATION_PLAN.md](plans/REAL_MRI_VERIFICATION_PLAN.md).

---

### Brick shape flexibility 완료 (2026-07-09) — Tier 1 후보에서 선택

- R4 baseline 이 노출한 `+7754%` 얇은 볼륨 오버헤드 문제를 정면 해결.
  Per-axis L0 brick 저장 크기 도입: `pageGrid.axis == 1` 인 축은 halo
  없이 `volSize.axis` 만 저장, 다른 축은 기존 64+1 halo=66 유지. 결과:
  - fMRI 64×64×10: atlas 66³ → **64×64×10**, +602% → +113%
  - 매머그래피 256×1024×1: **+6919% → +6%**
  - CT 512×512×1: **+6919% → +6%**
  - Siemens MR 484×484×1: **+7754% → +19%**

  단일-슬라이스 케이스 70× 메모리 절감. 시각 회귀 없음. Streaming pack
  이 여전히 66³ 를 쓰므로 Option C 는 Static 모드에만 적용, Streaming
  L1..L3 per-axis 는 후속. C++/UBO/5 셰이더 쌍 (WGSL + GLSL) 동시 갱신,
  그리고 `atlasBytesAllocated()` 가 Static 모드에서 L1..L3 phantom 을
  안 집계하도록 수정. 베이스라인:
  [BASELINE_2026-07-09_BRICK_SHAPE.md](baselines/BASELINE_2026-07-09_BRICK_SHAPE.md).

---

### Last-brick shrink 완료 (2026-07-09) — Option C 후속

- Option C 잔여 오버헤드 (+6~19%) 를 close. Static + `atlasGrid == pageGrid`
  구성에서 slot 을 position-based (slot == pageIdx) 로 전환하고, 각
  multi-brick 축의 마지막 브릭이 outer halo 를 버리고 interior 를 실제
  remainder 로 축소. 결과:
  - Siemens MR 484×484×1: **+19% → +6%** (atlas 528² → 499²)
  - fMRI 64×64×10: HUD 계산 drift 도 함께 해결, +113% → **-0.0%** (실측)
  - CT 512×512×1 / Mammo 256×1024×1: 이미 64 정렬이라 outer halo 만 감소
    (+6% → +6%, marginal)

  네 시리즈 모두 dense 대비 ≤ +6%. 잔여는 각 축당 1 halo 층 (첫 브릭의
  inner halo) 인데 clamp-to-edge sampling 을 유지하려면 필요한 이론적
  하한. UBO 에 `atlasPhys0` uvec4 추가, 6 셰이더 쌍이 UV 정규화 시
  이 값을 참조. Streaming pack + L1..L3 는 여전히 uniform 66³ (후속).
  베이스라인:
  [BASELINE_2026-07-09_LAST_BRICK_SHRINK.md](baselines/BASELINE_2026-07-09_LAST_BRICK_SHRINK.md).

---

### 스코프 재정비 (2026-07-10) — 원 목표 lens 재정렬

**원 목표 재확인** (사용자 명시):

> 현재 의료업계는 고사양 워크스테이션 하나에서만 대용량 CT/MRI 를 확인하는
> 형태다. 이걸 사양이 낮은 PC · 모바일 · 태블릿 등에서도 WebGPU + WASM 을
> 이용해 렌더링해서 쉽게 볼 수 있게 하는 것 = **접근성 축의 성능 차별화**.

**스코프 경계**:

- Mini-Engine 은 **렌더링/그래픽 엔진 프로젝트**. 임상 워크플로우 도구
  (MPR 3분할 · 측정 ruler/angle/ROI · DICOM 워크리스트 · 주석 · 십자선
  동기화 등) 는 **이 프로젝트 스코프 밖**. 필요하면 상위 앱 프로젝트로 분리.
- 성공 기준: **저사양 하드웨어 (Intel iGPU 노트북 · Chromebook · Android/iOS
  모바일) 에서 대용량 실 CT/MRI** 가 유의미한 fps/메모리로 도는가.

이전 실 MRI 검증 트랙 (R1-R4, 2026-07-07~2026-07-08) 은 완료 —
사용자가 자신의 DICOM 폴더를 브라우저에 드래그하여 렌더까지의 경로를
데스크톱에서 확보. Option C + Last-brick shrink (2026-07-09) 로 얇은 볼륨
오버헤드 정착. 그러나 **저사양·모바일** 축에서는 실측이 하나도 없음 →
이 재정비의 동기.

### 현재 위치 진단 (원 목표 lens)

지금까지의 인프라 트랙 (brick atlas · multi-LOD · streaming · disk paging ·
Option C · last-brick shrink · path-trace · empty-space skipping) 은 원 목표에
**정확히 부합** — 접근성 확대의 근간을 제공. 다만 **실측이 데스크톱 dGPU 에만
국한** 이 최대 제약.

| 갭 | 내용 |
| --- | --- |
| **A** (최대) | 저사양/모바일 실측 부재. Intel iGPU · Android · iOS 실측 = 0 |
| **B** | 브라우저 대용량 페이징 부재. 네이티브만 `MmappedFile`, 브라우저는 memfs 전량 로드 → WASM heap 4 GB 한계 |
| **C** | 실 임상 규모 데이터 실측 부재. pydicom-data 484×484×1 이 검증 최대 |
| **D** | 사양별 자동 policy 부재. 옵션 (atlas · LOD · path-trace · denoise · K budget) 모두 개발자 수동 |

### 유예 항목 재평가

원 목표 lens (저사양·모바일 접근성) 로 다시 보면 이전 유예 판단이 다수 뒤집힘:

| 항목 | 이전 판단 | 재평가 |
| --- | --- | --- |
| **P3.2 adaptive SPP by motion** | 유예 | **★★★★ 저사양 GPU 이동 시 fps 유지 핵심** |
| **Async CPU pack** (worker) | 유예 | **★★★★ 저사양 CPU · 모바일 main thread 부담 감소** |
| **Streaming per-axis** (Option C 확장) | 유예 | **★★★★ 브라우저 대용량 시 메모리 절감** |
| **Adaptive K budget** | 유예 | ★★★ 정착 후 부하 감소, 배터리·모바일 |
| P3.3 SVGF | 유예 | ★★ 인프라 큼, P3.2 로 대체 가능 |
| LOD 경계 seam 완화 | 유예 | ★★ 시각 아티팩트, 저사양 접근성엔 marginal |
| HDR equirect IBL | 유예 | ★ 여전히 부수적 |
| JPEG-LS | 유예 | ★ 임상 희귀 transfer syntax, 성능 무관 |

### 세 개의 축

#### 축 X: 저사양/모바일 실측 (갭 A · D 정면)

| Step | 작업 | 예상 |
| --- | --- | --- |
| **X1** ✅ | 모바일 WebGPU 지원 매트릭스 조사 (Android Chrome / iOS Safari 상태 · feature flag) — [계획서](plans/X1_MOBILE_WEBGPU_MATRIX.md) | 완료 |
| **X2** ✅ | 실측 하네스 — adapter info + GPU tier heuristic + baseline 자동 캡처 (`?bench=1&dwell=N`) | 완료 |
| **X3** 🟡 | 실 기기 실측 + baseline 문서 — [BASELINE_2026-07-14_MOBILE_MATRIX.md](baselines/BASELINE_2026-07-14_MOBILE_MATRIX.md) 에 T-high · T-mobile-high(a) iPhone KakaoTalk WKWebView · T-mobile-high(b) iPhone Safari.app 26.5 캡처. Android + Intel iGPU 는 append 예정 | 진행 중 |
| **X4** | 사양 티어별 자동 policy (LOD 상한 · path-trace on/off · denoise 세기 · K budget) | 2 세션 |

#### 축 Y: 브라우저 대용량 페이징 (갭 B · C 정면)

| Step | 작업 | 예상 |
| --- | --- | --- |
| **Y1** | File System Access API + Streams — chunk read (memfs 전량 로드 회피) | 2 세션 |
| **Y2** | IndexedDB / OPFS — decoded brick cache (재방문 시 파싱 스킵) | 2 세션 |
| **Y3** | TCIA 공개 대용량 CT (~300 MB, 512×512×500+) 로드/streaming 실측 + baseline | 1~2 세션 |

#### 축 Z: 저사양 렌더 최적화 (재평가된 유예)

| Step | 작업 | 예상 |
| --- | --- | --- |
| **Z1** ✅ | Adaptive SPP by camera motion (구 P3.2) — 드래그 중 SPP↓ 로 저사양 fps 유지. 커밋 `8ce5a9b`; RTX 4070 검증 완료; iPhone Safari 상호작용 중 mean 9-10 ms (정지 pt_spp8 38 ms 대비 4× 회복) | 완료 |
| **Z2** | Async CPU pack (worker thread) — Streaming 진입 시 main thread frame time 회복 | 2 세션 |
| **Z3** | Adaptive K budget — 정착 후 upload/frame↓ | 0.5 세션 |
| **Z4** | (선택) Streaming per-axis — Option C 를 Streaming pack 까지 확장 | 2~3 세션 |

### 명시적 폐기 · 저순위

원 목표 (저사양·모바일 접근성) lens 로 우선순위가 낮음. 필요 시 별도 결정으로 재개:

- **임상 워크플로우 도구화** — MPR 3분할 · 측정 (ruler/angle/ROI) · DICOM
  워크리스트 · 주석 · 십자선 동기화 등. **Mini-Engine 스코프 밖.** 필요하면
  상위 앱 프로젝트로 분리.
- **HDR equirect IBL** (구 M4 v2 P4) — 렌더링 미술. 저사양 접근성엔 부수적.
- **JPEG-LS transfer syntax** — 임상 희귀 케이스. 성능 무관.
- **M4 v2 P3.3 SVGF temporal reprojection** — 인프라 크고 P3.2 (Z1) 로 상당 부분 대체 가능.
- **LOD 경계 seam 완화** — 시각 아티팩트. 저사양 접근성엔 marginal.
- **CPU pack SIMD (B-2)** — interior memcpy 는 이미 컴파일러가 vectorize.
- **Screen-pixel-aware LOD selection** — 현 distance 임계값이 충분한 정도.

### 현재 활성 방향 (2026-07-10~)

**X → Z 순서로 진행 중** (X 실측 병렬로 Z 최적화 이미 착수):

- ✅ X1 · X2 · Z1 완료. X3 draft baseline 에 T-high + iPhone 두 브라우저 (KakaoTalk
  WKWebView · Safari.app 26.5) 캡처.
- 🟡 X3 append 대기 (Android Chrome / Intel iGPU — 기기 확보 시).
- ⏭ **다음 코드 세션 후보**: Z2 (async CPU pack, main thread 부담 감소) 또는
  X4 (tier auto policy) 또는 Y1 (브라우저 File System Access chunk paging).
  현재 데이터로 Z2 가 iPhone pt_spp8+denoise 42ms 개선 후속 케이스에 적절.
- pt_spp4 브라우저 격차 (관측 4) 는 정성적 항목으로 baseline follow-up 유지.

**다음 세션 진입점**: 이 재정비 섹션. 인프라 세부는 §2 마일스톤 참조. 사용자
인터페이스 / 빌드 / 조작은 [VIEWERS.md](VIEWERS.md). 코드 진입은
`src/rendering/VolumeRenderer.{hpp,cpp}` + `src/rendering/BrickedVolume.{hpp,cpp}`
(엔진 코어), `tests/volume_viewer.cpp` (네이티브 뷰어), `tests/volume_viewer_wasm.cpp`
(브라우저 뷰어).

### 지난 R4 트랙 참조 (완료 기록)

- 실 MRI 검증 트랙 (R1-R4, 2026-07-07~08) — 계획서
  [REAL_MRI_VERIFICATION_PLAN.md](plans/REAL_MRI_VERIFICATION_PLAN.md),
  결과 [BASELINE_2026-07-08_REAL_MRI.md](baselines/BASELINE_2026-07-08_REAL_MRI.md).
- Option C + Last-brick shrink (2026-07-09) — R4 가 노출한 얇은 볼륨 오버헤드
  갭을 해소하여 네 시리즈 모두 dense 대비 ≤ +6% 로 정착. Streaming per-axis
  확장은 축 Z4 로 이관.

---

## 6. 기존 로드맵과의 관계

- 본 문서는 [`CAREER_ROADMAP.md`](../../roadmap/CAREER_ROADMAP.md)의 Phase 7
  (Volume Rendering, 기초 구현 완료)을 **사업·차별화 방향으로 심화**한 트랙이다.
  CAREER_ROADMAP의 Cluster B(큐픽스/루닛/뷰노)·디지털 트윈 진입과 직접 맞물린다.
- [`ENGINE_ROADMAP.md`](../engine-roadmap/ENGINE_ROADMAP.md)(엔진 성숙도 트랙,
  A~D 완료; E 미진입)와 별개 트랙으로 공존. 본 트랙이 현재 활성 방향.
- 비고정 용어 규칙(CLAUDE.md §8): 본 문서는 "phase" 대신 "마일스톤(M1~M4)/
  단계/스텝"을 쓴다.
