# Current Active Work

진행 중인 작업 목록. 완료되면 `../archive/`로 이동.

---

## WebGPU Deferred Rendering Porting

**목표**: Vulkan showcase_demo와 동등한 시각적 결과물을 WebGPU/WASM에서 구현

| 문서 | 설명 |
|---|---|
| [WEBGPU_DEFERRED_PORTING_PLAN.md](webgpu-deferred/WEBGPU_DEFERRED_PORTING_PLAN.md) | Phase 0~6 구현 계획 |
| [DEFERRED_RENDERING_TROUBLESHOOTING.md](webgpu-deferred/DEFERRED_RENDERING_TROUBLESHOOTING.md) | 포팅 중 발생한 이슈 및 해결 기록 |

### 진행 상황

| Phase | 내용 | 상태 |
|---|---|---|
| 0 | Push Constant Emulator | ✅ 완료 |
| 1 | G-Buffer WGSL + GBufferPass 활성화 | ✅ 완료 |
| 2 | Deferred Lighting WGSL + DeferredLightingPass 활성화 | ✅ 완료 |
| 3 | Bloom 렌더패스 | ✅ 완료 |
| 4 | SSAO 컴퓨트 | ✅ 완료 |
| 5 | 통합 PostProcess Pass | ✅ 완료 |
| 6 | 가드 정리 & 프레임 루프 통합 | ✅ 완료 |
