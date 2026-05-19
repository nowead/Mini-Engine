# Current Active Work

진행 중인 작업 목록. 완료되면 `../archive/`로 이동.

---

## WebGPU Showcase 격상

**목표**: 네이티브 수준으로 완성된 WebGPU 렌더링 파이프라인을 쇼케이스를 통해 인터랙티브하게 시연

| 문서 | 설명 |
| --- | --- |
| [WEBGPU_SHOWCASE_PLAN.md](webgpu-showcase/WEBGPU_SHOWCASE_PLAN.md) | WebGPU UI/인터랙션 격상 구현 계획 |

### 진행 상황

**원안 Task (1차 계획):**

| 작업 | 내용 | 상태 |
| --- | --- | --- |
| 1 | Emscripten bindings — Renderer setter를 JS에 노출 | ✅ 완료 |
| 2 | HTML overlay UI — Debug view, Post-process, 조명 패널 | ✅ 완료 |
| 3 | WGSL CSM cascade 색상 오버레이 | ⚠️ 그림자 재설계로 대체 (단일 맵+PCSS, 시각화 무의미) |
| 4 | 씬에 동적 점 광원 배치 (도로 교차점 가로등 9개) | ✅ 완료 |
| 5 | 패스 타이밍 표시 | ◐ CPU 근사 부분 완료 — 진짜 GPU timestamp-query 미완 |

**포트폴리오 격상 (2026-05-19, [§5](webgpu-showcase/WEBGPU_SHOWCASE_PLAN.md#5-다음-단계-포트폴리오-수준-격상-권장)):**

| 작업 | 내용 | 상태 |
| --- | --- | --- |
| P0.1 | 프레이밍 레이어 — 인트로 모달 (기술 설명 + 기능 태그 + Tour/Explore 버튼) | ✅ 완료 |
| P0.2 | 가이드 투어 — 9단계 자동 시퀀스 (G-Buffer/SSAO/광원 0→9/Bloom), 토스트 UI | ✅ 완료 |
| P1.4 | Cascade 시각화 정리 — 오해 유발 "CSM Cascades" 체크박스 제거 | ✅ 완료 |
| +    | 랜딩 페이지 — `localhost:8000/`이 데모 인덱스로 연결 (`build_wasm/index.html`) | ✅ 완료 |
| P0.4 | 디퍼드 결정적 증명 극적화 — 광원 9 → 100 (Tier1 iconic 9 + Tier2 colored 91), G-Buffer 평탄 확인 | ✅ 완료 |
| P0.3 | 진짜 GPU `timestamp-query` — `WebGPUTimer` + RHI encoder pending state setter, `(GPU ms)` 라벨 자동 전환 | ✅ 완료 |
| P1.1 | A/B 분할 화면 — SSAO·점광원 유/무 좌/우 동시 비교 | ⬜ 미착수 |
| P1.2 | 비-WebGPU 폴백 영상 — Safari·모바일·구형 브라우저용 30초 캡처 | ⬜ 미착수 |
| P1.3 | 깊이 레이어 링크 — CHANGELOG + RHI 아키텍처 문서 데모 페이지에서 링크 | ⬜ 미착수 |

> **P0 모두 완료** — WebGPU 챕터의 핵심 신뢰성 격상 완료. 남은 P1 작업
> (A/B 분할 / 폴백 영상 / 깊이 레이어 링크)은 격 상승 옵션. 트랙 B
> (Phase 4 Bindless)로 전환해 다음 커리어 마일스톤(42dot 핵심 타겟)
> 추진하는 것도 자연스러운 선택.

---

## 완료된 작업 → archive

| 폴더 | 내용 |
| --- | --- |
| [archive/refactoring/webgpu-deferred/](../archive/refactoring/webgpu-deferred/) | WebGPU Deferred Rendering 포팅 (Phase 0–6 완료) |
