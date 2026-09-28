# 현재 상태와 다음 작업

현재 중심은 **공유 Vulkan/WebGPU 렌더링 코어를 활용한 CT/MRI 볼륨 시각화와 메모리 최적화**입니다. 마지막 기능 개발 기록은 2026-07-22이며, 이 인덱스는 2026-09-28에 코드와 기록을 대조해 정리했습니다.

## 구현된 범위

| 영역 | 현재 상태 | 근거 |
| --- | --- | --- |
| 장면 렌더링 | glTF/PBR 머티리얼, deferred, TAA, 그림자, GPU culling | [엔진 로드맵](engine-roadmap/ENGINE_ROADMAP.md) |
| 볼륨 뷰어 | Native + WASM, NIfTI/DICOM, ray march/PT, 누적·denoise | [사용 가이드](medical-volume/VIEWERS.md) |
| 메모리 관리 | Brick atlas, LRU/LOD, native NIfTI mmap, 축별 brick shape | [볼륨 트랙](medical-volume/README.md) |
| 실제 샘플 검증 | 공개 MR·CT·JPEG 샘플, 브라우저 DICOM 업로드, HUD | [실데이터 baseline](medical-volume/baselines/BASELINE_2026-07-08_REAL_MRI.md) |
| 모바일 로딩 | 슬라이스 단위 디코드, slab staging, progressive brick fill | [512³ 합성 CT baseline](medical-volume/baselines/BASELINE_2026-07-22_Y1C_FULL.md) |
| 자동 검증 | ThreadPool, NIfTI/raw, DICOM 로더를 CTest로 실행 | [테스트 가이드](../../tests/README.md), [최근 검증](../VALIDATION.md) |

## 아직 남은 범위

- iPad와 내장 GPU 장치에서 재측정. iPhone의 합성 256 MiB 로드 결과를 다른 장치나 임의의 임상 데이터로 일반화하지 않기.
- 모바일 atlas·LOD·upload budget 정책 확장.
- Streaming의 LOD 경계 seam, 가시 브릭이 예산을 넘을 때 missing brick, stale LOD 개선.
- 브라우저 대용량 디스크 페이징. 현재 progressive DICOM은 Static atlas 경로.
- JPEG-LS, 더 넓은 DICOM 포맷·공간 정보 처리, GPU 이미지 회귀 검증.

장치별 목표는 [모바일 티어 결정](medical-volume/decisions/ADR-2026-07-22_MOBILE_TARGET_TIERING.md)에 있습니다. 다음 후보와 구현 완료 항목은 [의료 볼륨 로드맵](medical-volume/MEDICAL_VOLUME_ROADMAP.md)에서 구분해 읽습니다.

## 기록을 찾아가는 경로

- [볼륨 트랙 인덱스](medical-volume/README.md): 계획·설계 결정·측정 기록 전체 목록
- [엔진 성숙도 로드맵](engine-roadmap/ENGINE_ROADMAP.md): glTF, TAA, 병렬 커맨드 기록의 개발·측정 과정
- [변경·디버깅 이력](../archive/changelogs/): 원인 분석과 수정 기록
- [문서 홈](../README.md): 외부 독자를 위한 시작점
