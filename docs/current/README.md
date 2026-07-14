# Current Active Work

진행 중인 작업 목록. 완료되면 `../archive/`로 이동.

---

## 현재 활성 방향 — 의료 볼륨 렌더링 (2026-05-29~)

WebGPU 기반 차세대 클라이언트 사이드 **의료 볼륨 렌더러**로 엔진을 심화하되,
네이티브 Vulkan을 한 기능도 빠짐없이 동등하게 유지(dual-backend parity).

| 문서 | 설명 |
| --- | --- |
| [MEDICAL_VOLUME_ROADMAP.md](medical-volume/MEDICAL_VOLUME_ROADMAP.md) | 전략 결정(방향 1) + 격차 진단 + 마일스톤 M1~M4 + 결정·진행 기록 |
| [VIEWERS.md](medical-volume/VIEWERS.md) | 사용자 가이드 — `volume_viewer` (네이티브) + `volume_viewer_wasm` (브라우저) 빌드·조작·기능·기술 스택 |
| [M3-3_V1_STREAMING_PLAN.md](medical-volume/plans/M3-3_V1_STREAMING_PLAN.md) | M3-3 v1-α 설계서 + 완료 기록 (v1-1~v1-4) |
| [V1_BETA_AND_MR_PLAN.md](medical-volume/plans/V1_BETA_AND_MR_PLAN.md) | M3-3 v1-β LOD + MR + CPU pack 트랙 계획서 (β-1~β-6 atomic) |
| [DICOM_IMPLICIT_VR_PLAN.md](medical-volume/plans/DICOM_IMPLICIT_VR_PLAN.md) | DICOM Implicit VR LE 파서 + tag dictionary + 검증 결과 |
| [DICOM_COMPRESSED_PLAN.md](medical-volume/plans/DICOM_COMPRESSED_PLAN.md) | DICOM 압축 transfer syntax (RLE + JPEG 2000) 4 step 계획 + 검증 |
| [DISK_PAGING_PLAN.md](medical-volume/plans/DISK_PAGING_PLAN.md) | M3-3 v1-β 디스크 페이징 Steps 1-3 (voxel source 추상화 + mmap + on-the-fly mip) |
| [DISK_PAGING_STEP5_PLAN.md](medical-volume/plans/DISK_PAGING_STEP5_PLAN.md) | Disk paging Step 5 (mmap int16 → brick-pack 시 변환, 진짜 4 GB+ unlock) |
| [WASM_OPENJPEG_PLAN.md](medical-volume/plans/WASM_OPENJPEG_PLAN.md) | WASM OpenJPEG 빌드 (브라우저 JPEG 2000 DICOM 디코드) |
| [DICOM_JPEG_LEGACY_PLAN.md](medical-volume/plans/DICOM_JPEG_LEGACY_PLAN.md) | DICOM JPEG Baseline / Extended / Lossless P14·SV1 (libjpeg-turbo, 네이티브) |
| [WASM_LIBJPEG_TURBO_PLAN.md](medical-volume/plans/WASM_LIBJPEG_TURBO_PLAN.md) | WASM libjpeg-turbo 빌드 (브라우저 JPEG legacy) |
| [PATH_TRACE_POLISH_PLAN.md](medical-volume/plans/PATH_TRACE_POLISH_PLAN.md) | M4 v2 path-trace 폴리시 — IBL + A-trous denoiser + accumulation cap (P1..P3.1 완료; P3.2/P3.3 유예) |
| [REAL_MRI_VERIFICATION_PLAN.md](medical-volume/plans/REAL_MRI_VERIFICATION_PLAN.md) | **Active** — 실 MR 번들 + preset 튜닝 + 런타임 DICOM 업로드 + FPS/메모리 HUD (R1..R4) |
| [BASELINE_2026-06-03.md](medical-volume/baselines/BASELINE_2026-06-03.md) | v0 + M4 v1 기준선 측정 |
| [BASELINE_2026-06-04_V1_ALPHA.md](medical-volume/baselines/BASELINE_2026-06-04_V1_ALPHA.md) | v1-α 측정 (auto-size win + streaming 자동 진입 + 정직한 한계) |
| [BASELINE_2026-06-07_V1_BETA.md](medical-volume/baselines/BASELINE_2026-06-07_V1_BETA.md) | v1-β LOD 측정 (missing brick 2320→326 -86%) + 알려진 한계 (LOD seam, stale-LOD blur) |
| [BASELINE_2026-06-07_DISK_PAGING.md](medical-volume/baselines/BASELINE_2026-06-07_DISK_PAGING.md) | Disk paging Steps 1-3 measurement (1024³ 정착 RAM 2.69→2.38 GB, 피크 6.57 GB 한계) |
| [BASELINE_2026-06-10_DISK_PAGING_STEP5.md](medical-volume/baselines/BASELINE_2026-06-10_DISK_PAGING_STEP5.md) | Disk paging Step 5 measurement (1024³ 피크 6.57→2.30 GB -65%, 16 GB RAM에서 8 GB 데이터 가능성) |
| [BASELINE_2026-07-08_REAL_MRI.md](medical-volume/baselines/BASELINE_2026-07-08_REAL_MRI.md) | 실 MRI 검증 (R4) — 4개 공개 시리즈, 런타임 업로드 경로 · atlas / CPU baseline · thin-volume overhead 관측 |
| [BASELINE_2026-07-09_BRICK_SHAPE.md](medical-volume/baselines/BASELINE_2026-07-09_BRICK_SHAPE.md) | Brick shape flexibility (Option C) — 얇은 볼륨 overhead +7754% → +6~19%, 단일-슬라이스 케이스 70× 메모리 절감 |
| [BASELINE_2026-07-09_LAST_BRICK_SHRINK.md](medical-volume/baselines/BASELINE_2026-07-09_LAST_BRICK_SHRINK.md) | Last-brick shrink (Option C 후속) — Siemens +19% → +6%; 네 R4 시리즈 모두 ≤ +6% vs dense |
| [BASELINE_2026-07-14_MOBILE_MATRIX.md](medical-volume/baselines/BASELINE_2026-07-14_MOBILE_MATRIX.md) | 축 X3 — RTX 4070 + iPhone iOS 18.7 (KakaoTalk WKWebView · Safari.app 26.5). iOS 18 WebGPU 기본 활성 · 저부하 iPhone > RTX · pt_spp8+denoise iPhone 24-26 fps · pt_spp4 브라우저 3× 격차 · Z1 verify 노트 |

**M1 (실데이터 기반) 완료** (2026-05-29): R16Float 16비트 · window/level ·
NIfTI 로더 + float 강도 경로 + 임상 윈도우 프리셋.
**M2 (시네마틱 품질) 완료** (2026-05-29): gradient 셰이딩 + 볼류메트릭 소프트
섀도우(양 백엔드, 토글).
**M3-1 (empty-space skipping) 완료** (2026-05-29): 컴퓨트 min/max occupancy
그리드 + 마칭 셀 스킵(양 백엔드).
**독립 WASM 볼륨 뷰어 완료** (2026-05-29): 브라우저에서 볼륨만 풀스크린 렌더하는
별도 실행파일(`volume_viewer_wasm`) — M1/M2/M3 전부 + HTML 컨트롤. 랜딩 인덱스에서
클릭 진입.
**M3-2 측정 완료** (2026-05-31): sparse 256³ 합성에서 skip +3~5%. 정직히 modest —
이 워크로드는 셰이딩 비용 우세. per-cell-entry GPU 최적화 시도는 warp divergence로
회귀(되돌림). 부수 산출물: VMA staging pool oversize 픽스, 합성 생성기 구조 크기 인자.
**DICOM 로더 완료** (2026-05-31): Explicit VR LE 단일 시리즈(int16 CT/MR) 파서 +
viewer 디렉토리 dispatch. `Volume3D` 공용 구조체로 NIfTI/DICOM 동일 경로. 합성
DICOM 생성기로 NIfTI와 크로스 검증(동일 입력 → 동일 출력).
**M4 v0 path-traced 산란 완료** (2026-05-31): Woodcock 자유경로 + Henyey-Greenstein
위상함수 + single-light NEE + inline SPP 평균(누적 버퍼 없음, v1로 보류). 두 번째
파이프라인을 march와 같은 bind group layout으로 추가 → 런타임 모드 스위치. 양 백엔드.
**실 임상 DICOM 4종 로드 성공** (2026-06-01): `scripts/fetch_sample_ct.py`로 pydicom
공개 코퍼스에서 4개 받아 검증. 발견·수정한 실세계 파서 갭: (1) **NumberOfFrames**
지원(multi-frame DICOM의 N프레임을 z 슬라이스로 펴기), (2) **중첩 undefined-length
SQ skip**(item-walk으로 정확히, 단순 byte scan은 inner delimitation에 걸려 픽셀
데이터를 지나침). 실 데이터로 검증된 범위: HU CT(128²), 고해상도 MR(1024²),
multi-frame MR(64²×10), enhanced CT(512²×2). 양 백엔드 엔진 무변경(파서 레벨만).
**M4 v1 progressive 누적 완료** (2026-06-02): 정지 카메라에서 매 프레임 path-trace
결과를 RGBA16Float ping-pong 텍스처에 running-mean으로 누적, 두 번째 패스가
tonemap만 담당. 카메라/윈도우/프리셋/SPP/anisotropy/bounces/모드/리사이즈 변경 시
N=0 reset(N=0이면 history×0이라 텍스처 clear 불필요). path-trace는 자체 bind-group
layout — depth/occupancy 제거, history 추가. WebGPU `textureLoad`는 sampler 미사용,
GLSL `texelFetch`는 sampler 필요 → display 바인딩이 백엔드별로 갈림.
**M3-3 v0 bricked storage 완료** (2026-06-02): 단일 dense 3D 텍스처 → brick atlas
(64³ interior + 1-voxel halo = 66³ stored) + page table(storage buffer of uint32
slot indices, 0xFFFFFFFF = "air") 간접 참조로 교체. 모든 density read가 셰이더
helper `sampleVolume(uvw)` 거쳐 page-lookup → linear atlas 샘플로 진행 (3 셰이더
세트: march · pathtrace · occupancy — 양 백엔드). 빈 brick(interior 전체 = 최솟값)은
atlas에 안 올라가 → 1024³ × 10% 점유 시 2GB → ~200MB의 가치. v0는 로드 타임 단발
packing. 부수: GLSL march UBO에 누락돼 있던 `pathtrace` 필드 발견·정렬.
**M3-3 v1-α streaming 완료** (2026-06-04): LRU + incremental brick upload +
memory-budget auto-size. Atlas 자동 산정 재작성 — `ceil(cbrt(nonEmpty))` 시작점,
512MB budget shrink. nonEmpty > slots 시 자동 Streaming, 매 프레임 K=8 brick
페이지인, LRU가 visible bumped slot 보호 (churn 방지). Streaming 진입 시
LOG_WARN로 권장 atlasGrid 안내. 1024³ default 280→188 MB(-33%), 2 GB dense 자동
streaming(493 MB). 정직한 한계: 가시 brick > atlas slots면 시각 hole — 줌아웃
전체보기 케이스는 v1-β LOD가 본질 해결. 발견·기록: atlas thrashing 디버깅 여정
([CHANGELOG_2026-06-04](../archive/changelogs/CHANGELOG_2026-06-04.md)).
**M3-3 v1-β LOD multi-resolution 완료** (2026-06-07): mip chain CPU build →
per-LOD atlas allocation → distance-based per-brick LOD selection → multi-LOD
streaming(no migration + LOD fallback + K=8→64 upload budget) → 셰이더
(`sampleVolume`)가 page table에 인코딩된 `(lod<<30)|slot`을 디코드해서 4 LOD
atlas 중 선택된 것을 샘플링. 1024³ dense Case C zoom-out: missing brick
2320 → 326 (~21% hole, **-86%**). Atlas memory L0 단독 493.5 MB → 4 LOD 합
~572 MB (+16%, 1/8+1/64+1/512 = 1.16× ratio). 정직한 한계: LOD 경계 seam(인접
brick LOD 다를 때 sampling 불연속), no-migration 결정으로 stale-LOD blur 일부.
**DICOM Implicit VR LE 완료** (2026-06-07): 임상 PACS의 기본 transfer syntax.
file meta(group 0002, 항상 Explicit VR)와 dataset 인코딩 분리 + tag → VR
dictionary로 implicit walker 구현. 합성 32³ + 256×256×128 양 인코딩 비트 동일,
pydicom 실 RT 파일(rtplan / rtdose) dispatch 검증(`bitsAllocated 32` 디코드가
파서 정확성 신호). 16비트 image-bearing Implicit VR 공개 샘플은 TCIA NBIA 인증
필요 — 정직히 기록.
**DICOM 압축 transfer syntax 완료** (2026-06-07): encapsulated PixelData walk +
RLE Lossless 인트리 PackBits 디코더 + OpenJPEG vcpkg 의존으로 JPEG 2000
(Lossless + Lossy). pydicom MR_small.dcm ↔ MR_small_RLE.dcm 비트 동일성
([127, 2145] range). 693_J2KI.dcm (CT 512×512 JPEG 2000 lossy) range
[-3995, 1812]. JPEG Baseline / Lossless / JPEG-LS + WASM OpenJPEG 빌드는 후속.
**Disk paging Steps 1-3 완료** (2026-06-07): HalfDataView 추상화 + NIfTI mmap
(`utils::MmappedFile` 포터블 wrapper) + `m_mipChain` 제거 + `packBrickToStaging`
LOD 박스 필터 즉석. 1024³ dense 정착 working set 2.69 → 2.38 GB (-11%), 파일
본체 ~2.1 GB는 OS 페이지 캐시로 이동. 피크는 여전히 6.57 GB — Volume3D float
intermediate + halfData 변환 임시 버퍼 동시 존재가 지배. 베이스라인:
[BASELINE_2026-06-07_DISK_PAGING.md](medical-volume/baselines/BASELINE_2026-06-07_DISK_PAGING.md).
**Disk paging Step 5 완료** (2026-06-10): VoxelSource 추상화 (HalfFloat / Int16
/ Uint16) → `buildFromMmappedSource` → NIfTI int16/uint16 mmap → brick-pack 시
직접 변환. **1024³ dense 피크 working set 6.57 → 2.30 GB (-65%)**. Volume3D
float intermediate + halfData 변환 임시 + m_originalHalfData 세 항목 모두 제거.
Static-fit 케이스는 자동 fallback. 베이스라인:
[BASELINE_2026-06-10_DISK_PAGING_STEP5.md](medical-volume/baselines/BASELINE_2026-06-10_DISK_PAGING_STEP5.md).
**WASM OpenJPEG 완료** (2026-06-10): Emscripten 빌드에 OpenJPEG FetchContent +
DicomFile.cpp 연결 + pydicom `693_J2KI.dcm`(CT 512x512 JPEG 2000 lossy)
build-time 다운로드 + preload. 브라우저(`volume_viewer_wasm`)에서 실 임상
CT를 OpenJPEG로 디코드 + 렌더 검증. WASM payload 738 KB → 1.08 MB (+345 KB
OpenJPEG 정적 링크). 부수 발견: WASM 뷰어 Streaming 모드 + ASYNCIFY + WebGPU
swapchain "Destroyed texture used in a submit" 검증 spam — Static atlas
워크어라운드 적용, 본질 해결은 후속 트랙. 계획서:
[WASM_OPENJPEG_PLAN.md](medical-volume/plans/WASM_OPENJPEG_PLAN.md).

**WASM Streaming + swapchain race 본질 해결 완료** (2026-06-13, `f353356`):
WASM `render()`가 `updateBrickStreaming`을 `beginFrame()` *이전*으로 이동 —
swapchain 텍스처 획득과 staging map 의 ASYNCIFY suspend 윈도우가 더는 겹치지
않음. Streaming 모드 강제 우회 워크어라운드 제거.
**DICOM JPEG legacy (네이티브) 완료** (2026-06-16, `6ff902c` → `a8c17e8`):
libjpeg-turbo vcpkg 의존 + `decodeJpegFrame16` precision 분기로 8/12/16-bit
처리 + 단일-frame multi-fragment 병합 + parseSlice dispatch. JPEG Baseline
(.50), Extended (.51), Lossless P14 (.57), SV1 (.70) 4종. pydicom JPGExtended
[0,264] / JPEG-LL [0,278] 검증, JPEG 2000 / RLE 무회귀. 계획서:
[DICOM_JPEG_LEGACY_PLAN.md](medical-volume/plans/DICOM_JPEG_LEGACY_PLAN.md).
**WASM libjpeg-turbo (브라우저 JPEG legacy) 완료** (2026-06-17, `9f83230` →
`053cc2b`): Emscripten 빌드에 libjpeg-turbo 3.1.2 FetchContent + `jpeg-static`
링크 + 동일 DicomFile.cpp 재사용. Upstream `add_subdirectory` FATAL_ERROR
우회 3가지 (manual populate + `string(REPLACE)` 패치 / `GNUInstallDirs` /
`uninstall` 타겟 rename). 브라우저 콘솔에서 JPEG-LL [0,278] 디코드 확인.
WASM 1.08 → 1.41 MB (+327 KB). 계획서:
[WASM_LIBJPEG_TURBO_PLAN.md](medical-volume/plans/WASM_LIBJPEG_TURBO_PLAN.md).
**단일-슬라이스 DICOM 시각화 폴리시 완료** (2026-06-17, `a3a4133`):
DICOM row flip (anatomy upright) · `Camera::setOrbit` · 조건부 HU 윈도우
(CT만) · 가장 얇은 halfExtent 최소 0.1 padding · 비-CT는 Cloud preset ·
`vol.d==1`이면 face-on 카메라. JPEG-LL이 디폴트 설정 그대로 보임.
**M4 v2 P1 path-trace IBL 완료** (2026-06-17, `521a3f8`): VolumeUBO에
envTop/envBot 추가 + `sampleEnvironment(dir)` top-bottom 그래디언트 sky +
`tracePath()` 양쪽 miss 분기에서 환경광 기여. WGSL + GLSL 미러.
**M4 v2 P2.1/P2.2 spatial denoiser 완료** (2026-06-17, `c34d85c` → `74b2473`):
path-trace ↔ display 사이에 third fragment pipeline 삽입 + 5x5 cross-bilateral
A-trous (color guide, stride=4) + UI 토글. 계획서:
[PATH_TRACE_POLISH_PLAN.md](medical-volume/plans/PATH_TRACE_POLISH_PLAN.md).
**M4 v2 P2.3 A-trous cascade + swapchain race fix 완료** (2026-07-07,
`5f6723a`): stride 1/2/4 ping-pong 3-iteration cascade + stride UBO
per-iter + swapchain dangling pointer 재획득 방어 (F12 crash 근본 수정).
**M4 v2 P3.1 accumulation N cap 완료** (2026-07-07, `d50e76f`): denoise
활성 시 N 을 32로 캡 → spatial denoise 가 정지 상태에서도 상시 가시
contribution. HUD 라인 + cap 슬라이더 + reset 버튼. **M4 v2 P3의 원 목표
달성.**

**현재 활성 트랙**: **실 MRI 검증 트랙 (2026-07-07~)** — 계획서:
[REAL_MRI_VERIFICATION_PLAN.md](medical-volume/plans/REAL_MRI_VERIFICATION_PLAN.md).
프로젝트 원 목표 ("실제 MRI 유의미한 프레임/메모리로 띄우기") 정면 대응.
R1 실 MR 번들 → R2 preset 튜닝 → R3 런타임 DICOM 업로드 → R4 FPS/메모리
HUD + baseline 측정.

**유예된 트랙**: M4 v2 P3.2 (adaptive SPP by motion) · M4 v2 P3.3
(SVGF temporal reprojection) · M4 v2 P4 (HDR equirect IBL) · JPEG-LS
(charls) · DICOM mmap (Phase C: 슬라이스 어셈블 캐시) · 즉흥 폴리시
(LOD seam 완화 / async pack / adaptive K / screen-pixel LOD selection).

**선행 완료**: 볼륨 렌더링 기초(3D 텍스처 + 레이마칭, Vulkan + WebGPU) ·
TF 프리셋 · 독립 뷰어(네이티브 `volume_viewer` + 브라우저 `volume_viewer_wasm`).

---

## 엔진 성숙도 로드맵 (2026-05-20~)

쇼케이스 격상이 종결된 시점에서 **엔진 자체의 성숙도** 관점으로 다시 그린 다음
단계 계획.

| 문서 | 설명 |
| --- | --- |
| [ENGINE_ROADMAP.md](engine-roadmap/ENGINE_ROADMAP.md) | 진단 + 작업 단위 5종 비교 + 권장 시퀀스(A → B → C → D → E) + 첫 작업(머티리얼 텍스처 확장) 구체화 |

**현재 진입 작업**: 로드맵 **D — 멀티스레드 커맨드 레코딩** (Vulkan 전용, 2026-05-26 착수).
패스 단위 병렬 기록(워커 스레드가 각자 primary CB에 기록 → 메인이 순서대로 제출).
단계: ~~D1~~ ✅ → ~~D2~~ ✅ → ~~D3 병렬 패스 기록(셰도우 4 캐스케이드)~~ ✅ →
~~D4 CPU 기록 시간 측정~~ ✅ — **로드맵 D 종결**. D4 결론: 셰도우는 인스턴스드 드로우라
기록이 O(1)이라 병렬 이득 없음(기록-바운드 아님); 능력 구현 + 측정로 규명. 상세:
[ENGINE_ROADMAP §D](engine-roadmap/ENGINE_ROADMAP.md).

**완료된 시퀀스**: AB(glTF + PBR) → Vulkan parity → sub-task 8(다중 메시) →
C(TAA, Vulkan + WebGPU) → 그림자 품질(하드웨어 PCF). 상세는 archive changelog 참조.

---

## 완료된 작업 → archive

| 폴더 | 내용 |
| --- | --- |
| [archive/refactoring/webgpu-showcase/](../archive/refactoring/webgpu-showcase/WEBGPU_SHOWCASE_PLAN.md) | WebGPU Showcase 격상 (2026-05-14 ~ 2026-05-20) |
| [archive/refactoring/webgpu-deferred/](../archive/refactoring/webgpu-deferred/) | WebGPU Deferred Rendering 포팅 |

최근 변경 이력: [CHANGELOG_2026-06-04](../archive/changelogs/CHANGELOG_2026-06-04.md)
(M3-3 v1-α LRU streaming + memory-budget auto-size · atlas thrashing 진단 + WARN) ·
[06-02](../archive/changelogs/CHANGELOG_2026-06-02.md)
(M3-3 v0 + M4 v1 양 백엔드 검증 · EMSDK env 전파 트랩 · OccUBO bind size 미동기화 트랩) ·
[05-27](../archive/changelogs/CHANGELOG_2026-05-27.md)
(D2 스레드 풀 · D3-0a/0b 장애물 해소 · D3-1/2 셰도우 캐스케이드 병렬 기록 · D4 측정·종결) ·
[05-26](../archive/changelogs/CHANGELOG_2026-05-26.md)
(TAA Vulkan+WebGPU · 그림자 하드웨어 PCF) · [05-25](../archive/changelogs/CHANGELOG_2026-05-25.md)
(그림자/컬 깜빡임 수정) · [05-24](../archive/changelogs/CHANGELOG_2026-05-24.md) (A/B · Vulkan parity · 다중 메시)
