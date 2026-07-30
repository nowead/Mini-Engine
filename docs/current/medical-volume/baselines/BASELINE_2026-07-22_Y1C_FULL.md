# Baseline 2026-07-22 — Y1c-full progressive · iPhone 256 MB CT

**축**: Y (브라우저 대용량 페이징) · **단계**: Y1c-full 완료 ·
**결과**: iPhone Safari 에서 **256 MB CT (512×512×512) 로드 성공** — Y1f 로
못 뚫었던 iPhone 티어 상한을 확장. ADR-2026-07-22 mobile-phone tier 목표
("표준 흉부/복부 CT ~256 MB") 달성.

## Y1c-full 요약

Y1f (`874a714`) 는 slab staging 으로 GPU alloc 시점 peak 을 낮췄지만
`halfData` (full-volume packed half, 볼륨 크기에 비례) 는 계속 남아있었음.
Y1c-full 은 그 halfData 자체를 제거:

- **BrickedVolume progressive API** — `beginProgressiveBuild` → `pushSlice(z, sliceHalf)` × N → `endProgressiveBuild`.
  Ring buffer (66 slices = brickStored L0 깊이) + 브릭-행 완성 트리거 (row's
  expected slice count 도달 시) + Y1f slab staging 재활용.
- **DicomFile progressive path** — `loadDicomSeriesProgressive(dir, meta, sink)`.
  Pass 1 은 Y1b 그대로, Pass 2 는 슬라이스 하나 decode → sink 호출 → memfs
  unlink. 로컬 slice buffer (W×H uint16) 한 번 alloc 재사용.
- **VolumeRenderer wrappers** — `beginProgressiveBuild` / `pushProgressiveSlice`
  / `endProgressiveBuild` — BrickedVolume 에 forward + post-load state
  finalise (occupancy grid, bind groups, window/level defaults).
- **양 viewer wire-up** — WASM: `applyDicomProgressive(dir, halfExtent)` 헬퍼.
  Native: DICOM 경로를 progressive callback 으로. NIfTI 는 legacy float path
  유지.

## 실측 결과 (iPhone iOS 18.7 Safari, Cloudflare tunnel, 2026-07-22)

### 256 MB (count 512, 512×512×512 synthetic CT)

```
Volume: 512x512x512  (256.0 MB dense)
Page:   8x8x8  (512 bricks)
Atlas:  8x8x8  304/512 slots
Memory: 165.8 / 279.2 MB  (+9% overhead)
CPU:    1.56 ms/frame  (mean 3.20 / max 16.78 over 60 samples)
Mode:   Static
Visible: 512 bricks (304 non-empty)
Accum:  N=0 (cap 32)
```

**Y1f 시점 (같은 wasm+device)**: 로드 시도 시 crash 예상 (peak ~701 MB) —
`halfData` 128 + slab 44 + GPU 279 + runtime 250 = 701 MB.

### Native 검증 (RTX 4070 Chrome / Vulkan, 512 MB 볼륨)

```
[BrickedVolume] Y1c-full progressive begin: 512x512x1024 -> page 8x8x16,
                atlas 527x527x1055, slab 34 MB, ring 33 MB
[Dicom] loaded (progressive) build_wasm/synth_ct (512x512x1024, ...
        range [-1000,800], 1024 files, 1024 frames)
[BrickedVolume] Y1c-full progressive built 512x512x1024 -> page 8x8x16
                (304 non-empty), 304 atlas slots
```

512 MB dense 볼륨 (하반부 sphere) 도 native 에서 정상 로드. Y1f 의 halfData
512 MB 가 사라지고 ring 33 MB + slab 34 MB 만 transient. Streaming path (Y1f
그대로) 는 무회귀.

## Peak 메모리 흐름 비교 (256 MB 볼륨, WASM 추정)

| Phase | Y1f | Y1c-full |
|---|---:|---:|
| DICOM Pass 2 진입 (intensityHalf 없음 vs 있음) | 250 | 250 |
| Full halfData alloc (Y1c-lite `intensityHalf`) | +128 = 378 | (skipped, ring only) |
| Ring buffer alloc | -- | +33 = 283 |
| Slab staging alloc | +44 = 422 | +44 = 327 |
| GPU L0 atlas alloc | +279 = **701** ← **crash** | +279 = **606** |
| Copy + waitIdle × N slabs | 701 유지 | 606 유지 |
| Post-load steady state | (crashed) | GPU 279 + runtime 250 = 529 |

**Peak 701 → 606 MB (-95 MB)**. iPhone iOS ~600-650 MB 예산 안으로 진입 확정.

## Slab / Ring 상세

Ring 크기는 볼륨의 **W × H 에만 비례** (Z 와 무관). 256 MB / 512 MB / 1 GB
볼륨 모두 W=H=512 라면 ring = 66 × 512 × 512 × 2 = **~34 MB** 로 일정.

Slab staging 크기는 W × H × brickStored (66) × 2, WebGPU 256-정렬 padding
포함 → **~44 MB** for W=512.

GPU atlas 는 볼륨 크기에 비례 (sparse 하면 usedSlots 만큼 실효 데이터).
Peak 은 결국 GPU 알로케이션 크기가 지배 → 볼륨 크기가 물리적 상한 결정.

## 원 목표 도달도 재확인 (티어링 관점, ADR-2026-07-22)

| Tier | 목표 사이즈 | 상태 |
|---|---|---|
| iPhone 티어 | 표준 흉부 CT (~192 MB) | ✅ (Y1f) |
| iPhone 티어 | 표준 임상 CT (~256 MB) | ✅ **(Y1c-full 실증 2026-07-22)** |
| iPad Pro / 저사양 데스크톱 | 완전 흉부/복부 CT (~512 MB) | 🟡 desktop Chrome 512 MB Static 성공 확인, iPad 실측 대기 |
| 데스크톱 | 전신 CT (500 MB - multi-GB) | ✅ (native mmap Step 5) |

ADR 상 iPhone 티어의 종반 스텝 완료. 다음은 tier 실측 (iPad + Intel iGPU
laptop) 로 넘어감.

## Follow-ups

1. **~~Y1c-full-1 API 스켈레톤~~ ✅** (동 커밋)
2. **~~Y1c-full-2 ring + row pack 구현~~ ✅** (동 커밋)
3. **~~Y1c-full-3 DicomFile progressive + VolumeRenderer wrappers~~ ✅** (동 커밋)
4. **~~Y1c-full-4 viewer wire-up + baseline~~ ✅** (본 문서)
5. **UI Copy stats safe-area 픽스** — 동 커밋에 포함
   (iOS Safari 하단 탭이 panel 하단 컨트롤 덮음 해결)
6. **iPad Pro / Chromebook / Intel iGPU 실측** — 다음 세션 후보
7. **X4 v2 (모바일 티어 policy)** — atlas / LOD / K budget 을 티어별로

## 알려진 한계 · 유의점

- **Empty-brick 검사 heuristic**: progressive path 는 slice 도착 전에 begin
  하므로 `dataMinGuess = -1000.0` (CT air) 로 empty pattern 결정. 비-CT
  (MR, PET 등) 볼륨은 진짜 dataMin (예: 0) 과 다르면 일부 실제 빈 브릭이
  slot 배정됨 → GPU 슬롯 낭비, 렌더링 자체는 정확. 실 CT 는 -1000 이라
  영향 없음.
- **Static-only**: Streaming path 는 여전히 Y1c-lite 경로 (`m_originalHalfData`)
  유지. 이유: Streaming 브릭 재-decode 위한 source 가 필요한데, Y2
  (IndexedDB) 가 아직 없음. ADR 상 iPhone 전신 CT 는 아웃-오브-스코프라
  Streaming 확장 유예.
- **NIfTI path 무변경**: NIfTI 는 mmap 기반 (native) 또는 절차적 (WASM) 이라
  progressive 필요성 낮음.

## 재현

**필수 재생성** (기존 파일이 count 와 D header 불일치일 수 있음):
```powershell
python scripts/make_synthetic_dicom.py build_wasm/synth_ct 512 512 512
```

**iPhone Safari 테스트**:
1. `.\scripts\wasm.ps1 build -Target volume_viewer_wasm`
2. `python scripts\serve_nocache.py 8000`
3. iPhone: URL 접속 → 오른쪽 패널 → Count `512` → Load → HUD Copy stats 로 결과 공유.
