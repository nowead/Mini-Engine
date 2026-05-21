# Current Active Work

진행 중인 작업 목록. 완료되면 `../archive/`로 이동.

---

## 엔진 성숙도 로드맵 (2026-05-20~)

쇼케이스 격상이 종결된 시점에서 **엔진 자체의 성숙도** 관점으로 다시 그린 다음
단계 계획.

| 문서 | 설명 |
| --- | --- |
| [ENGINE_ROADMAP.md](engine-roadmap/ENGINE_ROADMAP.md) | 진단 + 작업 단위 5종 비교 + 권장 시퀀스(A → B → C → D → E) + 첫 작업(머티리얼 텍스처 확장) 구체화 |

**현재 진입 작업**: AB(통합) — glTF 2.0 ingest + PBR 머티리얼 파이프라인.
cgltf로 파싱·바이너리 디코딩 위임, 해석 레이어(`AssetImporter`) 직접 구현.
첫 마일스톤: DamagedHelmet.glb 로딩 후 PBR 텍스처(노멀 · MR · 이미시브) 렌더링.

---

## 완료된 작업 → archive

| 폴더 | 내용 |
| --- | --- |
| [archive/refactoring/webgpu-showcase/](../archive/refactoring/webgpu-showcase/WEBGPU_SHOWCASE_PLAN.md) | WebGPU Showcase 격상 (2026-05-14 ~ 2026-05-20) |
| [archive/refactoring/webgpu-deferred/](../archive/refactoring/webgpu-deferred/) | WebGPU Deferred Rendering 포팅 |

최근 변경 이력: [CHANGELOG_2026-05-20](../archive/changelogs/CHANGELOG_2026-05-20.md)
