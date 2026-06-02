# Mini-Engine — 의료 볼륨 렌더링 방향 계획서

**작성일**: 2026-05-29
**관점**: 엔진을 **WebGPU 기반 차세대 클라이언트 사이드 의료 볼륨 렌더러**로
끌고 가되, **네이티브 Vulkan을 한 기능도 빠뜨리지 않고 동등하게(dual-backend
parity)** 유지한다. WebGPU는 *배포·시연*의 차별화이고, Vulkan은 *개발·검증·
프로파일링*의 1차 백엔드다 — 모든 기능은 Vulkan에서 검증 레이어로 먼저
완성하고 WebGPU로 포팅한다.

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

**결정 기록**:

- *실파일 포맷*: **NIfTI 우선으로 결정·완료**(`d4f9659`). DICOM은 다음 단계로.
- *WASM 데모 분리*: **별도 실행파일로 결정·완료**(`7089d84`). 쇼케이스에 묻히던
  볼륨 가시성 문제 해소. (디버깅: ASYNCIFY 재진입 가드 + DOM 핸들러의 직접 wasm
  호출 제거 — 데이터 범위를 `Module._dataMin/Max`로 push.)
- *Skip 일반화 시도*: **per-cell-entry 변형은 GPU warp divergence로 회귀 → 되돌림**
  (2026-05-31). CPU 직관과 반대 — 원본 per-sample 체크는 cache가 read를 무료화해
  더 빠름. 셰이더 주석에 기록.

**다음 진입 작업**: **M4 v1 — 누적 버퍼로 progressive convergence**. 정지 카메라
에서 매 프레임 샘플을 history 텍스처에 누적, 카메라/파라미터 변경 시 reset.
TAA 인프라(이미 보유)와 같은 ping-pong 패턴. v0의 노이즈가 사라지고 진짜 시네마틱
정지 이미지가 됨.

**남은 후보**:

- **M3-3 bricking** — VRAM 초과 볼륨 페이징("1GB+" 주장의 본체), 작업량 큼.
- **DICOM Implicit VR LE 지원** — 임상 PACS의 기본 transfer syntax. 추가 시 실
  병원 데이터 커버리지 크게 확장.
- **DICOM 압축 transfer syntax**(JPEG/JPEG2000/RLE) — libjpeg 등 외부 의존 필요.

**다음 세션 진입점**: 이 문서 §2(마일스톤 M3-2/M4 상세) + 위 후보 목록. 코드
진입은 `src/rendering/VolumeRenderer.{hpp,cpp}`(엔진 코어), `tests/volume_viewer.cpp`
(네이티브 뷰어), `tests/volume_viewer_wasm.cpp`(브라우저 뷰어).

---

## 6. 기존 로드맵과의 관계

- 본 문서는 [`CAREER_ROADMAP.md`](../../roadmap/CAREER_ROADMAP.md)의 Phase 7
  (Volume Rendering, 기초 구현 완료)을 **사업·차별화 방향으로 심화**한 트랙이다.
  CAREER_ROADMAP의 Cluster B(큐픽스/루닛/뷰노)·디지털 트윈 진입과 직접 맞물린다.
- [`ENGINE_ROADMAP.md`](../engine-roadmap/ENGINE_ROADMAP.md)(엔진 성숙도 트랙,
  A~E 완료)와 별개 트랙으로 공존. 본 트랙이 현재 활성 방향.
- 비고정 용어 규칙(CLAUDE.md §8): 본 문서는 "phase" 대신 "마일스톤(M1~M4)/
  단계/스텝"을 쓴다.
