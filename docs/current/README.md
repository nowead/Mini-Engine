# Current Active Work

진행 중인 작업 목록. 완료되면 `../archive/`로 이동.

---

## WebGPU Showcase 격상

**목표**: 네이티브 수준으로 완성된 WebGPU 렌더링 파이프라인을 쇼케이스를 통해 인터랙티브하게 시연

| 문서 | 설명 |
| --- | --- |
| [WEBGPU_SHOWCASE_PLAN.md](webgpu-showcase/WEBGPU_SHOWCASE_PLAN.md) | WebGPU UI/인터랙션 격상 구현 계획 |

### 진행 상황

| 작업 | 내용 | 상태 |
| --- | --- | --- |
| 1 | Emscripten bindings — Renderer setter를 JS에 노출 | ✅ 완료 |
| 2 | HTML overlay UI — Debug view, Post-process, 조명 패널 | ✅ 완료 |
| 3 | WGSL CSM cascade 색상 오버레이 | ⚠️ 그림자 재설계로 대체 (단일 맵+PCSS, 시각화 무의미) |
| 4 | 씬에 동적 점 광원 배치 (도로 교차점 가로등 9개) | ✅ 완료 |
| 5 | 패스 타이밍 표시 | ◐ CPU 근사 부분 완료 — 진짜 GPU timestamp-query 미완 |

> Task 3은 2026-05-19 그림자 전면 재작성(CSM 폐기 → 단일 씬-고정 맵 + PCSS)으로
> 무의미해졌고, Task 5는 CPU 명령 기록 시간 근사로 부분 완료됐다. 포트폴리오
> 수준 격상을 위한 남은 작업(프레이밍·가이드 투어·GPU timestamp-query 등)은
> [WEBGPU_SHOWCASE_PLAN.md](webgpu-showcase/WEBGPU_SHOWCASE_PLAN.md) §5 참조.

---

## 완료된 작업 → archive

| 폴더 | 내용 |
| --- | --- |
| [archive/refactoring/webgpu-deferred/](../archive/refactoring/webgpu-deferred/) | WebGPU Deferred Rendering 포팅 (Phase 0–6 완료) |
