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
| [M3-3_V1_STREAMING_PLAN.md](medical-volume/M3-3_V1_STREAMING_PLAN.md) | M3-3 v1-α 설계서 + 완료 기록 (v1-1~v1-4) |
| [BASELINE_2026-06-03.md](medical-volume/BASELINE_2026-06-03.md) | v0 + M4 v1 기준선 측정 |
| [BASELINE_2026-06-04_V1_ALPHA.md](medical-volume/BASELINE_2026-06-04_V1_ALPHA.md) | v1-α 측정 (auto-size win + streaming 자동 진입 + 정직한 한계) |

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

**다음 후보**: M3-3 v1-β (LOD / CPU pack 최적화 / 디스크 페이징) · DICOM 후속
(Implicit VR LE 지원 시 임상 PACS 데이터 커버리지 ↑).

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
