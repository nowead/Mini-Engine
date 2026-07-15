# Baseline 2026-07-15 — Synthetic 128 MB CT (Y3 first-cut, desktop)

**축**: Y (브라우저 대용량 페이징) · **단계**: Y3 pre-flight · **상태**: **desktop
only** — 모바일 실측은 옵션 C (server fetch 유틸) 추가 후 append.

이 baseline 은 로드맵 재정비 (2026-07-10) 이후 처음으로 **100 MB 급 실 임상 사이즈**
볼륨을 우리 stack 에 통과시킨 결과. 지금까지 최대 검증 크기는 484×484×1
(0.5 MB) 로 원 목표의 "대용량" 이 완전 미검증 상태였음.

## Setup

- **볼륨**: 512×512×256 int16, Explicit VR LE, 256 files
- **생성 스크립트**: `scripts/make_synthetic_dicom.py` (커밋 `101ae71`)
  - 명령: `python scripts/make_synthetic_dicom.py <out> 512 512 256`
  - 생성 시간: ~2 s wall
  - 총 파일 크기: 130 MB (256 × ~508 KB, 512×512×2 pixel + 4 KB header 각)
  - 컨텐츠: 동심원 sphere 팬텀 (air / soft tissue / bone), pageGrid 축 정렬
- **하드웨어**: Windows 데스크톱, NVIDIA RTX 4070 (tier=high)
- **브라우저**: Chrome 150, `http://localhost:8000/volume_viewer_wasm.html`
- **하네스**: X2 셸 (Y1a sequential upload + X4 v1 policy)

## 측정 결과 (STATS 패널 + 업로드 status)

### 로드 시간

| Phase | Time |
| --- | ---:|
| Read + write (memfs) | **10.4 s** (Y1a sequential, 256 × ~40 ms/file) |
| Decode (parse + brick pack + GPU upload) | **842 ms** |
| **Total** | **~11.3 s** |

### Atlas · 메모리

| 항목 | 값 |
| --- | --- |
| Volume | 512×512×256 (128.0 MB dense) |
| Page grid | 8×8×4 = 256 bricks |
| Atlas grid | 8×8×4, 56/256 slots (~22% occupancy — sphere 팬텀 특성) |
| Atlas memory (used / allocated) | 30.5 / 139.3 MB |
| Overhead vs dense | **+9%** |
| Mode | **Static** (default 512 MB budget 안 여유) |

### 프레임 타임 (Lambert + shadow, 정지)

| 지표 | 값 |
| --- | ---:|
| CPU / frame (current) | 5.34 ms |
| Mean (500 ms 폴 링버퍼) | 7.83 ms |
| Max | 19.9 ms |

## 관측 · 해석

### 관측 1 — Y1a sequential 업로드 대용량에서 안정

10.4 s / 130 MB = ~12.5 MB/s (memfs write throughput). 시퀀셜 특성상 각 파일
`arrayBuffer()` + write 를 순차 처리 → JS heap peak 는 1 파일 (~500 KB) 로
유지. 대체된 Promise.all 방식이었다면 JS heap peak 130 MB 였을 것.

**UX 관점**: 10 s 는 짧지 않으나 진행률 표시 (Y1a) 로 사용자 인지 개선. `Reading
N/M files · X.X MB` 가 실시간으로 갱신됨.

**성능 관점**: 10 s 중 대부분은 memfs 쓰기가 아니라 브라우저의 파일 read API 자체
지연으로 추정 (256 개 별개 File 객체 read). 진짜 병목이면 `arrayBuffer()`
프로파일링 필요.

### 관측 2 — Static 모드, 512 MB 예산 안 여유

Atlas 139 MB / 512 MB budget = 27%. Streaming 자동 진입 로직 (`nonEmptyCount >
totalSlots`) 트리거 안 됨. 즉 이 크기까지는 **Static 경로가 전부 처리**.

**함의**:
- 축 Z 의 Z2 (async CPU pack) 대상 = Streaming per-frame pack. **이 크기에선
  Z2 targeted 케이스 발생 안 함** — Z2 는 여전히 관측 없이 인프라 짓기.
- X4 v2 (atlas MB cap · LOD cap · K budget) 도 동일. 이 크기엔 필요 없음.
- **~256 MB 이상 볼륨** 혹은 **모바일 저메모리 environment** 에서만 Streaming 진입 예상.

### 관측 3 — Option C + Last-brick shrink 이득 확인

128 MB dense → 139 MB atlas alloc = +9%. 완벽 dense 정렬 (512, 256 모두 64 배수)
이라 last-brick shrink 효과 없음 — halo 만 +9% 오버헤드. 이는 이론적 하한
(각 축 브릭 halo layer) 에 근접. 정합성 좋음.

### 관측 4 — CPU 5.34 ms Lambert 여유

RTX 4070 에서 128 MB 볼륨 Lambert+shadow 정지 프레임 5 ms → 200 fps. Max 19.9 ms
는 로드 직후 스파이크 or occasional GC 로 추정. Baseline 확보.

### 관측 5 — Y1b 필요성 판단 (desktop 데이터 기준)

- **Y1b 필요 = 아니오** (desktop 4070 조건에서). 130 MB 를 memfs 에 통째로 얹어도
  프로세스 안정, 로드 성공.
- **Y1b 필요 = 미정** (mobile 조건). WASM heap 은 32-bit 4 GB 상한이나 브라우저
  프로세스 예산은 훨씬 낮음 (iOS Safari 1-2 GB 추정). 130 MB 이 넘는 임상 CT
  (500+ 슬라이스, 300 MB+) 에선 memfs peak 이 문제일 수 있음.

**결정 정지**: 옵션 C (server fetch 유틸) 로 폰에서 같은 128 MB 를 로드 → mobile
결과 확인 후 Y1b 착수 여부 재판단.

## 대비 Prior baselines

| Series | Dims | dense | Atlas alloc | Overhead | CPU (dGPU) |
| --- | --- | --- | --- | --- | --- |
| mr_emri_small (fMRI) | 64×64×10 | 0.08 MB | 0.08 MB | -0.0% | 1.42 ms |
| mr_siemens_slice | 484×484×1 | 0.5 MB | 0.5 MB | +6% | 1.5 ms |
| **synth_ct 512×512×256** | **512×512×256** | **128 MB** | **139 MB** | **+9%** | **5.3 ms** |

**주목**: 크기가 256× 늘어도 프레임 타임은 4× 만 증가. GPU 는 대용량 sparse
브릭 atlas 를 잘 소화. CPU 시간은 파일 로드 phase 에서만 병목.

## Follow-ups

1. ~~**대용량 볼륨 실측** (X3 baseline follow-up 7)~~ ✅ **desktop 완료** (본 문서).
2. **옵션 C 구현**: 셸에 "Load from server URL" 유틸 추가 (~30 라인 JS + 10 라인
   HTML). 폰 재접속 만으로 반복 실측 가능 → 모바일 append.
3. **모바일 128 MB 실측**: 옵션 C 후 iPhone Safari.app + WKWebView 두 경로 실측.
   특히 memfs peak 안전 여부 (프로세스 kill 없이 로드 완료?).
4. **더 큰 사이즈 (512×512×500 = ~256 MB)**: 임상 CT 실 규모. Streaming 자동
   진입 여부 확인. 진입 시 X4 v2 · Z2 관측 데이터 확보.
5. **Path-traced 모드 성능**: 128 MB 볼륨에서 pt_spp1..pt_spp8 프레임 타임. X3
   fMRI 벤치는 tiny 볼륨이라 오버헤드-bound 였음; 실 크기에선 GPU-bound 로 전환
   예상.
6. **읽기 phase 프로파일링**: 10.4 s / 256 files = 40 ms/file. `arrayBuffer()`
   vs FS.writeFile 시간 분리해서 진짜 병목 확인. 만약 대부분이 브라우저
   File API 자체라면 Y1b 로도 못 줄임.
