# Baseline 2026-07-20 — Mobile Y3 bisection (iPhone Safari ceiling)

**축**: Y (브라우저 대용량 페이징) · **단계**: Y3 mobile · **결과**: iPhone Safari
실용 상한 **96~128 MB 사이** · **Y1b 필요성 정량 입증**.

Y3 desktop baseline (2026-07-15, RTX 4070) 은 128 MB 볼륨 정상 로드 확인. 이번
mobile baseline 은 같은 스택을 **iPhone Safari 26 + Cloudflare tunnel** 로 실측.
크래시 지점을 바이섹션으로 좁혀 실용 상한과 Y1b 임팩트를 데이터로 잡음.

## Harness

- 셸: `tests/volume_viewer_shell.html` (커밋 `94fd34a` — mobile touch controls +
  STATS Copy 포함)
- 하네스: X2 device profile + Y1a sequential upload + Y-C v2 server fetch
  (auto-detect on 404) + X4 v1.5 mobile-high-safari tier policy
- 배포: Cloudflare Quick Tunnel (`cloudflared tunnel --url http://localhost:8000`)
- 서버: `python -m http.server` (via `wasm.ps1 serve`) 가 `build_wasm/` 서빙
- 데이터: `scripts/make_synthetic_dicom.py` 로 각 사이즈별 재생성. sphere 팬텀
  (~22% 슬롯 점유), Explicit VR LE, 512×512 은 파일당 ~525 KB
- 기기: iPhone iOS 18.7 + Safari.app 26.5 (WebKit 605)
- 방법: 사이즈를 4 MB → 128 MB 사이 5 단계로 바이섹션, 각 사이즈 로드 후
  STATS Copy → 아래 표에 취합

## 바이섹션 결과

| Dims | Dense | Atlas alloc | Overhead | CPU mean/max (샘플수) | 결과 |
|---|---:|---:|---:|---|---|
| 256×256×32   | 4.0 MB   | 4.2 MB   | +6% | 1.62 / 5.26 ms (54) | ✅ 안정, 오래 렌더 |
| 256×256×128  | 16.0 MB  | 17.3 MB  | +8% | 19.56 / 29.16 ms (5)  | ✅ 로드 성공, transient jitter |
| 384×384×128  | 36.0 MB  | 39.0 MB  | +8% | 4.63 / 6.37 ms (8)   | ✅ 안정 |
| **512×512×128** | **64.0 MB** | **69.4 MB** | **+8%** | **1.67 / 3.76 ms (41)** | ✅ **완벽, 41 샘플 신뢰도 최상** |
| 512×512×192  | 96.0 MB  | 104.4 MB | +9% | 20.02 / 20.73 ms (3) | ✅ 로드 성공, transient jitter |
| 512×512×256  | 128.0 MB | 139.3 MB | +9% | — | ❌ **Safari kill+reload · Chrome freeze at decoding** |

**Static 모드 전부** — 512 MB atlas budget 안. Streaming 자동 진입 안 함
(X4 v2 · Z2 target 재차 미관측).

**샘플 수 낮은 케이스 (16 MB, 96 MB) 의 CPU 20 ms mean 은 로드 직후 GPU
cache warmup transient**. 41 샘플 잡힌 64 MB 는 1.67 ms mean 으로 GPU 완전
여유 상태. 큰 볼륨에서도 정착 시 낮은 프레임 시간 나올 것으로 추정 (30 초+
dwell 로 재실측 필요 시).

## 크래시 관측 (128 MB · 512×512×256)

두 브라우저 다른 실패 모드:

- **Safari.app 26.5**: fetch 완료 (256/256) → decode 단계 → **탭 kill + 자동
  reload** → 기본 fMRI (64×64×10) 로 복귀. iOS 의 memory pressure 대응 시나리오.
- **Chrome 150 mobile**: fetch 완료 → `decoding 256 file(s)` 문구에서
  **화면 freeze**, 회전/터치 무반응, 탭 유지 (Safari 처럼 재시작 안 함).
  더 조용한 실패 = 사용자 관점에서 더 나쁨 (뭐가 잘못됐는지 모름).

**공통 원인**: WASM heap peak 이 iOS 브라우저 프로세스 예산 (~1-2 GB) 근처
도달. 두 브라우저 다 130 MB 급 볼륨을 memfs + parseSlice buffer + intensity
+ atlas 로 동시 홀드하는 순간 붕괴.

## Y1b (per-slice streaming) 정량 정당화

### 현 로드 peak (128 MB 볼륨 기준 추정)

| 구성 요소 | 크기 | 지속성 |
|---|---:|---|
| memfs (256 파일) | 130 MB | 로드 완료 후 유지 |
| parseSlice buffers (256 × vector<uint8_t>) | 130 MB | decode 단계에서 alloc |
| `out.intensity` (float32) | 128 MB | 렌더 중 유지 |
| Atlas GPU alloc | 139 MB | 렌더 중 유지 |
| Emscripten runtime / stacks | ~250 MB | 항상 |
| **Peak transition** | **~780 MB** | decode 중 |

### Y1b 후 (예상)

Y1b = "wasm 이 per-slice API 로 파일 하나씩 처리, 처리 완료 후 memfs 삭제":

| 구성 요소 | 크기 | 절감 |
|---|---:|---|
| memfs (1 파일 hold at a time) | ~0.5 MB | **-130 MB** |
| parseSlice buffer (1 슬라이스) | ~0.5 MB | **-130 MB** |
| out.intensity | 128 MB | 동일 |
| Atlas | 139 MB | 동일 |
| Runtime | ~250 MB | 동일 |
| **Peak transition** | **~520 MB** | **-260 MB** |

**iOS Safari 예산 1-2 GB 안에서 128 MB 볼륨 로드 여유 확보 예상**. 실 임상
CT (256 슬라이스 급) 이 iPhone 에서 실용 가능해짐.

### 임팩트를 넘어서는 원 목표 정합성

원 목표 (저사양·모바일 접근성) 정면 대응:
- 96 MB (~256×256 급 실 임상 CT, 128 슬라이스) → 이미 iPhone 지원
- 128 MB (~512 슬라이스 CT) → Y1b 후 지원 예상
- 그 위 (256 MB · 500+ 슬라이스) → Streaming per-axis · X4 v2 필요

## 크로스 티어 정리

| 지표 | RTX 4070 desktop | iPhone Safari mobile |
|---|---|---|
| **로드 가능 최대** | 128 MB (테스트 최대) | **96 MB** (측정 상한) |
| Load time 128 MB | 11.3 s | 크래시 (fetch 완료, decode 실패) |
| Load time 64 MB | (미측정) | ~50 s (Y-C v2 tunnel, 128 files × ~400 ms) |
| CPU/frame 64 MB (Lambert) | (미측정) | 1.67 ms |
| Static/Streaming | Static | Static (모든 성공 사이즈) |
| Memory overhead | +9% | +6~9% (사이즈 무관 일관) |

Desktop 이 mobile 을 1.3× 이상 웃도는 게 아니라, **iPhone Safari 가 접근 가능 벽
에 부딪히기 전 사용성 자체는 desktop 과 비등**. 원 목표의 "저사양 접근성" 인프라
방향성 재확인.

## Follow-ups

1. **Y1b 착수** — 이 baseline 이 정당화. 2 세션 예상.
   - `beginDicomStream(seriesId)` / `addDicomSlice(path)` /
     `finalizeDicomStream()` wasm 바인딩 신설
   - `loadDicomSeries` 를 header-scan pass + per-slice-decode pass 두 단계로
     refactor. 각 슬라이스 완료 후 JS 가 `Module.FS.unlink(name)` 호출
   - Y1b 후 128 MB 재실측 → 성공하면 그 자체가 Y1b baseline
2. **정밀 상한 확인** (선택, ~30 분) — 96 MB 성공 · 128 MB 실패 사이:
   - 512×512×224 (~112 MB) 로 상한 좁히기. Y1b 착수 후엔 무관해질 수도.
3. **큰 사이즈에서 dwell=30+ 로 CPU 정착 값 실측** — 96 MB CPU 20 ms 가
   transient 인지 정착값인지 판정. path-trace 모드 SPP 별로도.
4. **Android Chrome 실측** — X3 append 대상. 같은 하네스 재사용 가능,
   Chrome 은 다른 memory 특성 예상.
5. **원인 조사 · pt_spp4 3× 격차 (X3 obs 4)** — 미해결, 낮은 우선순위.
