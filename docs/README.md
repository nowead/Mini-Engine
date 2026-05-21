# Mini-Engine Documentation

## 구조

```text
docs/
├── README.md          ← 이 파일 (전체 네비게이션)
├── SUMMARY.md         ← 프로젝트 요약
├── EVOLUTION.md       ← 프로젝트 전체 흐름 (거시적 히스토리)
│
├── current/           ← 현재 진행 중인 작업
│   └── README.md      ← 진행 상황 인덱스
│
├── roadmap/           ← 거시적 방향 / 비전
│   ├── CAREER_ROADMAP.md
│   ├── SHOWCASE_ROADMAP.md
│   └── OPTIMIZATION_AND_HARDWARE_ABSTRACTION.md
│
├── guides/            ← 시점 무관 참조 문서
│   ├── BUILD_GUIDE.md
│   └── webgpu/
│       ├── WASM_EMDAWNWEBGPU_MIGRATION.md
│       ├── features/
│       └── troubleshooting/
│
└── archive/           ← 완료된 과거 작업 기록
    ├── refactoring/
    │   ├── monolith-to-layered/
    │   ├── layered-to-rhi/
    │   ├── aaa-upgrade/
    │   ├── webgpu-backend/
    │   ├── webgpu-deferred/
    │   └── webgpu-showcase/
    ├── changelogs/
    ├── debugging/
    └── game_logic/
```

---

## 현재 진행 중

→ **[current/README.md](current/README.md)**

- [ENGINE_ROADMAP.md](current/engine-roadmap/ENGINE_ROADMAP.md) — 엔진 성숙도 관점의 다음 단계 계획서. 첫 작업: glTF 2.0 ingest + PBR 머티리얼 파이프라인(AB 통합, cgltf 채택).

---

## 거시적 방향

- [SHOWCASE_ROADMAP.md](roadmap/SHOWCASE_ROADMAP.md) — 쇼케이스 데모 로드맵
- [CAREER_ROADMAP.md](roadmap/CAREER_ROADMAP.md) — 커리어 방향
- [OPTIMIZATION_AND_HARDWARE_ABSTRACTION.md](roadmap/OPTIMIZATION_AND_HARDWARE_ABSTRACTION.md) — 최적화 및 하드웨어 추상화 비전

## 프로젝트 히스토리

- [EVOLUTION.md](EVOLUTION.md) — 프로젝트 전체 진화 과정

## 빌드 & 참조

- [BUILD_GUIDE.md](guides/BUILD_GUIDE.md) — 빌드 가이드
- [guides/webgpu/](guides/webgpu/) — WebGPU/WASM 관련 가이드

## 과거 작업 기록

- [archive/refactoring/monolith-to-layered/](archive/refactoring/monolith-to-layered/) — 1세대 모노리스 분리
- [archive/refactoring/layered-to-rhi/](archive/refactoring/layered-to-rhi/) — 2세대 RHI 마이그레이션
- [archive/refactoring/webgpu-backend/](archive/refactoring/webgpu-backend/) — WebGPU 백엔드 구축
- [archive/changelogs/](archive/changelogs/) — 변경 이력
