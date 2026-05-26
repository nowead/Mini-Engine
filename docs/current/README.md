# Current Active Work

진행 중인 작업 목록. 완료되면 `../archive/`로 이동.

---

## 엔진 성숙도 로드맵 (2026-05-20~)

쇼케이스 격상이 종결된 시점에서 **엔진 자체의 성숙도** 관점으로 다시 그린 다음
단계 계획.

| 문서 | 설명 |
| --- | --- |
| [ENGINE_ROADMAP.md](engine-roadmap/ENGINE_ROADMAP.md) | 진단 + 작업 단위 5종 비교 + 권장 시퀀스(A → B → C → D → E) + 첫 작업(머티리얼 텍스처 확장) 구체화 |

**현재 진입 작업**: 로드맵 **D — 멀티스레드 커맨드 레코딩** (Vulkan 전용, 2026-05-26 착수).
패스 단위 병렬 기록(워커 스레드가 각자 primary CB에 기록 → 메인이 순서대로 제출).
단계: ~~D1 RHI 스레드 안전 커맨드 풀~~ ✅ → ~~D2 스레드 풀~~ ✅ → **D3 RenderGraph
병렬 스케줄러** → D4 검증·측정. 상세: [ENGINE_ROADMAP §D](engine-roadmap/ENGINE_ROADMAP.md).

**완료된 시퀀스**: AB(glTF + PBR) → Vulkan parity → sub-task 8(다중 메시) →
C(TAA, Vulkan + WebGPU) → 그림자 품질(하드웨어 PCF). 상세는 archive changelog 참조.

---

## 완료된 작업 → archive

| 폴더 | 내용 |
| --- | --- |
| [archive/refactoring/webgpu-showcase/](../archive/refactoring/webgpu-showcase/WEBGPU_SHOWCASE_PLAN.md) | WebGPU Showcase 격상 (2026-05-14 ~ 2026-05-20) |
| [archive/refactoring/webgpu-deferred/](../archive/refactoring/webgpu-deferred/) | WebGPU Deferred Rendering 포팅 |

최근 변경 이력: [CHANGELOG_2026-05-27](../archive/changelogs/CHANGELOG_2026-05-27.md)
(D2 스레드 풀 · 동시성 버그 2종) · [05-26](../archive/changelogs/CHANGELOG_2026-05-26.md)
(TAA Vulkan+WebGPU · 그림자 하드웨어 PCF) · [05-25](../archive/changelogs/CHANGELOG_2026-05-25.md)
(그림자/컬 깜빡임 수정) · [05-24](../archive/changelogs/CHANGELOG_2026-05-24.md) (A/B · Vulkan parity · 다중 메시)
