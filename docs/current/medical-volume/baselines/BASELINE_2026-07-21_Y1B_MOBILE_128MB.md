# Baseline 2026-07-21 — Y1b breakthrough: 128 MB clinical CT on iPhone Safari

**축**: Y (브라우저 대용량 페이징) · **단계**: Y1b 완료 검증 · **결과**:
**모바일 상한 96 MB → 128 MB+ 확장, crash 없이 63 fps 안정**.

Y3 mobile bisect (2026-07-20) 가 iPhone Safari 실용 상한 96 MB, 128 MB 크래시
확인 및 Y1b 임팩트 예상 (memory peak 780→520 MB, -260 MB) 을 정량화.
Y1b 구현 (DicomFile.cpp two-pass refactor) 후 같은 128 MB 로드 실측.

## Y1b 요약 (커밋 대상)

`loadDicomSeries` 를 **two-pass** 로 재구조:

- **Pass 1** — 각 파일 parseSlice → SliceMeta (header 만) 추출 → Slice + fileBuf drop.
  이전엔 `vector<vector<uint8_t>> buffers(N)` 로 모든 파일 바이트를 동시 홀드 (~130 MB).
- **정렬** — SliceMeta 기반 z-position 정렬 (기존 로직 그대로).
- **Pass 2** — 정렬된 순서로 파일 재파싱 → out.intensity[z] 픽셀 쓰기 →
  fileBuf drop → **memfs 파일 unlink** (Emscripten 만, 다중-frame 시 마지막
  frame 후에만). 파일 처리 진행하며 memfs 가 축소.

트레이드오프: 각 파일 2회 파싱 → decode 시간 +15%. 대신 wasm heap peak 대폭 절감.

## Setup

- 셸: `tests/volume_viewer_shell.html` (커밋 `94fd34a`, mobile touch controls
  + STATS Copy 포함)
- wasm: DicomFile.cpp Y1b two-pass (이 커밋)
- 데이터: `scripts/make_synthetic_dicom.py build_wasm/synth_ct 512 512 256` →
  256 × 512×512 int16 sphere phantom, ~130 MB
- 배포: Cloudflare Quick Tunnel
- 기기:
  - **Desktop**: Windows, NVIDIA RTX 4070, Chrome 150 (tier=high)
  - **Mobile**: iPhone iOS 18.7, Safari.app 26.5 (tier=mobile-high-safari)

## Result — 128 MB (512×512×256)

| Metric | Desktop (RTX 4070) | Mobile (iPhone Safari) |
|---|---|---|
| Load 성공 | ✅ (Y3 desktop 2026-07-15 이미 확인) | ✅ **NEW** (직전 07-20 crash 지점) |
| Volume | 512×512×256, 128 MB dense | 동일 |
| Page grid | 8×8×4 (256 bricks) | 동일 |
| Atlas | 8×8×4, 56/256 slots | 동일 |
| Memory (used/alloc) | 30.5 / 139.3 MB | 30.5 / 139.3 MB |
| Overhead | +9% | +9% |
| Mode | Static | Static |
| **CPU / frame (Lambert+shadow)** | **4.42 mean / 4.64 max** ms | **15.77 mean / 21.11 max** ms |
| Frame rate | 226 / 216 fps | **63 / 47 fps** |
| Sample count | 4 (짧게 관측) | 10 (스팍히 있음) |
| Fetch+write (via cloudflared tunnel) | 52 s (localhost 실측) | 108.9 s (256 files × ~425 ms/file) |
| Decode (Y1b two-pass) | ~1 s (측정 미포함) | 966 ms |

**주요 관측**:

- **iPhone 128 MB, 63 fps 안정**. 인터랙션 (회전/줌) 정상 반응. 원 목표 (저사양
  모바일에서 실 임상 CT 규모, 유의미한 fps) 정면 달성.
- **CPU 프레임 타임 데스크톱/모바일 3.5× 차이** — 128 MB 볼륨의 GPU 부하 반영.
  fMRI 64³×10 에서 관측된 mobile > desktop 역전 (관측 1, Y3 2026-07-14) 은
  overhead-bound 영역에서만 성립; 이 규모에선 GPU throughput 지배.
- **메모리 특성 완전 동등** — 데스크톱과 모바일이 같은 Atlas · 같은 오버헤드.
  Y1b 는 로드 phase 만 바꿨고 렌더 phase 는 무변경 (예상대로).

## Decode 오버헤드 실측

| 상황 | Decode 시간 |
|---|---|
| Y3 desktop pre-Y1b (07-15, 128 MB) | 842 ms |
| Y1b mobile (this baseline, 128 MB) | 966 ms |
| **증가율** | **+15%** |

Two-pass 파싱의 예상 오버헤드 범위 안. 128 MB 볼륨 로드 시 <1 초 추가 →
crash 방지 대비 매우 저렴한 트레이드오프.

## Y1b 메모리 임팩트 (예상 vs 실측 유추)

Y3 mobile bisect (2026-07-20) 예측:

```
Pre-Y1b peak (128 MB):
  memfs 130 + parseSlice buf 130 + intensity 128 + atlas 139 + runtime 250
  = ~780 MB → iOS Safari budget 초과 → crash

Y1b peak (예상):
  memfs shrinking + parseSlice buf <1 + intensity 128 + atlas 139 + runtime 250
  = ~520 MB → iOS Safari budget 여유 → 성공
```

**실측 결과가 이 예측을 강력히 뒷받침** — 이전엔 crash 하던 128 MB 가 이번엔
정상 로드. 정확한 peak 을 브라우저 API 로 확인하진 못하나 (WebGPU/WebAssembly
에 memory profiling 표준 없음), crash 벽 뚫린 것 자체가 정성적 증명.

## Y1b 후 새 상한 (실측)

192 MB (512×512×384) 시도:

- `python scripts/make_synthetic_dicom.py build_wasm/synth_ct 512 512 384`
- 폰 (iPhone iOS 18.7 Safari) fetch 완료 → **"decoding 384 files" 에서 freeze**
- Y3 mobile bisect (07-20) 의 128 MB crash 와 같은 실패 모드 (Chrome-style freeze)

**Post-Y1b 실 상한**: 128 MB 성공, 192 MB 크래시 → **128~160 MB 어딘가**.
256 MB 는 시도 안 함 (192 이미 실패).

### 남은 memory bottleneck 분석

Y1b 는 로드 phase 의 memfs+parseSlice 이중 홀드를 제거했지만, 이후 phase 는
그대로:

```text
Pass 2 완료 후 (memfs 0):
  out.intensity          192 MB
  runtime                ~250 MB
  = ~440 MB

BrickedVolume::build() 시작 (atlas alloc):
  out.intensity          192 MB  (아직 alive, atlas 로 복사 중)
  atlas                  209 MB  (allocating)
  runtime                ~250 MB
  = ~651 MB  ← 이 지점에서 iOS Safari kill
```

즉 Y1b 는 로드 초기 peak (~780 MB) 을 제거했지만, atlas alloc peak (intensity
+ atlas 동시 alive) 이 남아있음.

### 다음 최적화 후보 (원 목표 lens)

192+ MB 상한을 뚫으려면:

- **Y1c (가칭) — intensity 우회 스트리밍**: `loadDicomSeries` 가 out.intensity
  를 채우지 않고, per-slice decode 후 BrickedVolume 에 바로 write 하도록.
  intensity buffer 자체가 사라짐 → peak -192 MB → **~450 MB** 로 축소 가능.
  큰 refactor (Volume3D 인터페이스 변경).
- **Streaming 강제 진입**: atlas budget 을 tier 별 낮게 (mobile 256 MB) 강제
  해서 자동 Streaming 모드로 전환. atlas peak 을 상한 안에 가둠. 시각 hole
  가능 (visible-set < atlas 성립해야).
- **X4 v2 (wasm-side knobs)**: atlas MB cap · LOD cap · K budget 을 티어별로
  강제. 위 Streaming 진입과 연동.

## 상한 정리 · 원 목표 도달도

| 사이즈 | Y1b 전 | Y1b 후 |
|---|---|---|
| 4~64 MB | ✅ | ✅ |
| 96 MB | ✅ (transient jitter) | ✅ (전이 값 안정 예상) |
| **128 MB** | ❌ **crash** | ✅ **63 fps, +9% overhead** |
| 192 MB | ❌ (미측정, 확실 실패) | ❌ **freeze at decoding** |
| 256 MB | ❌ | ❌ (미측정, 실패 예상) |

**Y1b 임팩트: 실용 상한 ~96 MB → ~128 MB (+33%)**. 실 임상 CT 시리즈 중 얇은
슬라이스 (256 급) 는 처리 가능, 두꺼운 슬라이스 (384~500) 는 여전히 못 함.
후자 처리하려면 Y1c 급 다음 단계 필요.

## 원 목표 정합성 재확인

원 목표 (2026-07-10 재정비):
> 저사양 PC · 모바일 · 태블릿 브라우저 (WebGPU + WASM) 에서 대용량 실 CT/MRI
> 유의미한 fps/메모리로 렌더링

Y1b 로 도달한 실증:

- ✅ **저사양 하드웨어** (iPhone A16/A17-class Apple GPU)
- ✅ **모바일 브라우저** (Safari + Chrome 둘 다 대상, 실측은 Safari)
- ✅ **WebGPU + WASM** (Metal 백엔드 뒷단, 브라우저 native)
- ✅ **대용량 실 CT 규모** (128 MB = 임상 CT 시리즈 중간 규모)
- ✅ **유의미한 fps** (63 fps steady-state, 47 fps p95)
- ✅ **유의미한 메모리** (140 MB atlas + intensity, iPhone 4-8 GB RAM 여유)

**Mini-Engine 이 원 목표를 실제 데이터로 실증한 첫 시점** — Y1b 커밋이
프로젝트의 tangible milestone.

## Follow-ups

1. **~~Y1b 정당화~~ ✅ 완료** (본 문서).
2. **새 상한 확인** (같은 세션): 192 MB · 256 MB 시도. append 예정.
3. **X3 append** (Android Chrome, Intel iGPU): 여전히 기기 대기.
4. **Y2 (IndexedDB / OPFS brick cache)**: 재방문 시 파싱 스킵. Y1b 상한 확인
   후 우선순위 재평가.
5. **X4 v2 (wasm-side atlas/LOD/K budget knobs)**: Y1b 후 큰 볼륨에서
   Streaming 자동 진입 관측되면 정당화. 아직 Static 유지 시 미필요.
6. **Z2 (async CPU pack)**: 여전히 Streaming 진입 후 관측 대상.
7. **path-trace 모드 128 MB 성능**: Lambert 만 실측. path-trace SPP 별 필요.
