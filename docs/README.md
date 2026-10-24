# Mini-Engine 문서

처음 방문했다면 [프로젝트 소개](../README.md) → [아키텍처](ARCHITECTURE.md) → [실행](guides/BUILD_GUIDE.md) 순서로 읽어보세요.

| 목적 | 문서 |
| --- | --- |
| 프로젝트가 무엇인지 파악하기 | [README](../README.md), [현재 구현 요약](SUMMARY.md) |
| 구조와 설계 판단 살펴보기 | [아키텍처](ARCHITECTURE.md) |
| 폴더 역할과 새 코드 추가 위치 찾기 | [코드 지도](guides/CODE_MAP.md) |
| 빌드하고 실행하기 | [빌드 가이드](guides/BUILD_GUIDE.md) |
| CT/MRI 데이터 열기 | [볼륨 뷰어 사용법](current/medical-volume/VIEWERS.md) |
| 자동 테스트·수동 검증하기 | [테스트 가이드](../tests/README.md) |
| 이번 정리에서 확인한 결과 보기 | [로컬 검증 기록](VALIDATION.md) |
| 측정 결과와 한계 확인하기 | [볼륨 트랙 인덱스](current/medical-volume/README.md), [측정 기록](current/medical-volume/baselines/) |
| 현재 상태와 남은 작업 보기 | [현재 작업](current/README.md) |
| 데이터·에셋 출처 확인하기 | [의존성·에셋 안내](ASSETS.md) |

## 문서의 역할

- **README / SUMMARY / ARCHITECTURE / guides**: 현재 사용자가 읽는 소개와 참조 문서입니다.
- **current/**: 진행 상태, 설계 결정, 계획, 날짜별 측정 기록입니다. 측정 기록의 수치는 해당 장치·빌드·데이터 조건에 한정됩니다.
- **roadmap/**: 장기 방향과 아이디어입니다. 구현 완료 목록이 아닙니다.
- **archive/**: 이전 구조, 마이그레이션, 디버깅 이력입니다. 예전 실행 명령과 기능 상태가 남아 있습니다.

## 더 깊이 읽기

- [볼륨 렌더링 입문과 코드 연결](current/medical-volume/learning/VOLUME_RENDERING_PRIMER.md)
- [모바일 장치별 목표 결정](current/medical-volume/decisions/ADR-2026-07-22_MOBILE_TARGET_TIERING.md)
- [엔진 성숙도 로드맵](current/engine-roadmap/ENGINE_ROADMAP.md)
- [프로젝트 진화 기록](EVOLUTION.md) — 작성 시점까지의 역사
- [RHI 기술 가이드](archive/refactoring/layered-to-rhi/RHI_TECHNICAL_GUIDE.md) — 초기 설계의 상세 기록
- [변경·디버깅 이력](archive/changelogs/)

문서 변경 시 현재 기능은 코드와 대조하고, 성능 주장은 조건이 있는 측정 기록으로 연결합니다. 날짜가 있는 과거 기록은 새 측정값으로 덮어쓰지 않습니다.
