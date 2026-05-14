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
| 3 | WGSL DeferredLighting — CSM cascade 색상 오버레이 | ✅ 완료 (셰이더 기구현) |
| 4 | 씬에 동적 점 광원 배치 (도로 교차점 가로등) | ✅ 완료 |
| 5 | CPU-side 패스 타이밍 표시 (HTML 패널 실시간 폴링) | ✅ 완료 |

---

## 완료된 작업 → archive

| 폴더 | 내용 |
| --- | --- |
| [archive/refactoring/webgpu-deferred/](../archive/refactoring/webgpu-deferred/) | WebGPU Deferred Rendering 포팅 (Phase 0–6 완료) |
