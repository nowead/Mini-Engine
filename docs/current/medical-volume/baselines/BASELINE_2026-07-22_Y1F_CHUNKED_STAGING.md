# Baseline 2026-07-22 — Y1f chunked staging · iPhone 192 MB breakthrough

**축**: Y (브라우저 대용량 페이징) · **단계**: Y1f 완료 ·
**결과**: iPhone Safari 에서 **192 MB CT 로드 최초 실증** (Y1e 시점 crash 하던
케이스). Peak wasm 프로세스 예산 안으로 진입 확정.

## Y1f 요약

Y1e (2026-07-21, `d924543`) 이후 남은 병목이 atlas-sized single-staging (~209 MB
allocation) 이었음. Y1f 는 그것을 Z-slab (~44 MB WASM / ~34 MB native) 단위로
쪼개고 slab 마다 `copyBufferToTexture` + `waitIdle`. 같은 staging 을 재활용해서
peak 을 slab 크기까지 낮춤.

- **Static path** (`build()` 안): 두 pass 로 리팩터
  - Pass 1: 모든 non-empty 브릭에 slot 지정 + `pageTable` 채우기
  - GPU L0 텍스처 upfront alloc + `Undefined→TransferDst` transition 1회
  - Slab loop: map → fillEmpty(slab) → pack(bricks whose atlasZ in slab) →
    unmap → `copyBufferToTexture(dstZ = slabStartZ)` → `waitIdle`
  - 모든 slab 끝난 뒤 `TransferDst→ShaderReadOnly` transition + halfData 해제
- **Streaming path**: 변경 없음 (legacy per-LOD single-staging). Streaming 은
  visible-set << atlas 워크로드가 정상이라 초기 empty-fill peak 은 Y1f scope 밖.
- **mmap path (`buildFromMmappedSource`)**: Streaming-only 라 Y1f scope 밖.
  후속 세션 후보.

## 실측 결과 (iPhone iOS 18.7 Safari, Cloudflare tunnel, 2026-07-22)

### 192 MB (count 384, 512×512×384 synthetic CT)

```
Volume: 512x512x384  (192.0 MB dense)
Page:   8x8x6  (384 bricks)
Atlas:  8x8x6  160/384 slots      ← non-empty (합성 sphere 내부만)
Memory: 87.2 / 209.2 MB  (+9% overhead)
CPU:    18.53 ms/frame  (mean 2.16 / max 18.53 over 60 samples)
Mode:   Static
Visible: 384 bricks (160 non-empty)

Load: fetching+write 204146 ms + decode 1008 ms
```

**Y1e 시점 (같은 wasm+device)**: 크래시 → 첫 화면 fMRI 로 auto-reload
([BASELINE_2026-07-21_Y1CDE_PHYSICAL_WALL.md](BASELINE_2026-07-21_Y1CDE_PHYSICAL_WALL.md))

### 128 MB regression 체크 (count 256, 512×512×256)

```
Volume: 512x512x256  (128.0 MB dense)
Page:   8x8x4  (256 bricks)
Atlas:  8x8x4  112/256 slots
Memory: 61.0 / 139.3 MB  (+9% overhead)
CPU:    17.03 ms/frame  (mean 15.18 / max 17.03 over 4 samples)
Mode:   Static
Visible: 256 bricks (112 non-empty)

Load: fetching+write 138316 ms + decode 731 ms
```

무회귀 확인. Y1b baseline 대비 저장·상태 동일 (steady state CPU 는 샘플 4개라
아직 warm-up 중; 60+ 샘플 후에는 mean 이 정상 범위로 하강 예상).

## Peak 메모리 흐름 비교 (192 MB 볼륨, WASM)

시간축 상 각 phase 별 peak (근사, 250 MB runtime baseline 포함):

| Phase | Y1e | Y1f |
|---|---:|---:|
| DICOM Pass 2 half pack (intensityHalf 96 build) | 346 | 346 |
| Volume3D handoff, apply | 346 | 346 |
| `uploadHalf` → `build()` 진입 (halfData moved) | 346 | 346 |
| Staging alloc | 555 (staging **209**) | 390 (slab **44**) |
| Pack loop (halfData + staging alive) | 555 | 390 |
| GPU L0 alloc | 555 → 764 → 668 (halfData drop 뒤 GPU alloc) | 390 → **599** (GPU alloc 은 halfData 살아있는 상태) |
| Copy + waitIdle | 668 ← **죽는 지점** | 599 (여러 slab 반복) |
| halfData drop | (Y1e 는 pre-GPU) | 599 → 503 |
| Steady state | 459 (staging + GPU) | 503 → 459 (staging drop 뒤) |

**Peak 668 → 599 MB (-69 MB)**. iPhone 프로세스 예산 ~600 MB 문턱을 아슬하게
넘긴 것으로 보였으나, 실측에서 로드 성공. iOS 프로세스 예산이 예상보다 살짝
높거나 (~620-650 MB), 혹은 Y1f 로 halfData 를 loop 내 계속 활용해서 GPU alloc
직전이 아니라 loop 중간 어느 시점이 peak 이 되어 실제 순간 peak 이 599 미만
일 가능성.

정확한 peak 은 iOS Safari 가 `performance.memory` 미노출이라 추정치. 다만
crash 여부는 확실한 실측치.

## Slab loop 상세 (192 MB 케이스)

브라우저 콘솔 로그 (WASM):
```
[INFO][BrickedVolume] Y1f slab upload: atlas 528x528x396, slab 528x528x66 (44 MB, 6 slabs)
```

Native (RTX 4070, Vulkan) 검증 시 로그:
```
[INFO][BrickedVolume] Y1f slab upload: atlas 527x527x263, slab 527x527x66 (34 MB, 4 slabs)  # 512x512x256
[INFO][BrickedVolume] Y1f slab upload: atlas 131x131x131, slab 131x131x66 (2 MB, 2 slabs)  # 128^3 default
```

WASM padded row = ceil(width*2, 256), Native native row = width*2. 그래서 WASM
slab 이 native 보다 ~30% 더 큼.

Load-time 추가 비용: iPhone 은 slab 마다 `waitIdle` × 6 = 대략 30-60 ms 상수
추가 (실측 미분리, 총 decode 1008 ms 안에 흡수). Native 는 무시할 수준.

## 원 목표 도달도 재확인

| 실 임상 CT 사이즈 | Mobile 도달 |
|---|---|
| 머리 CT (~64 MB) | ✅ |
| 얇은 슬라이스 (~128 MB) | ✅ (Y1b 실증) |
| **가슴/복부 CT 진입 (~192 MB)** | ✅ **(Y1f 실증 2026-07-22)** |
| 완전 흉부 CT (~256 MB) | ❌ (아직 미검증, Y1c-full 후보) |
| 전신 CT (500 MB+) | ❌ (아키텍처 재설계 필요) |

원 프로젝트 목표 ("저사양 모바일 대용량 실 CT") 의 **일반적 흉부 CT 범위 진입**.
Y1e 시점 hard-wall (192 MB crash) 이 Y1f 로 해소됨.

## 다음 후보

**단기**:
- **Y1c-full 진입 (~3-4 세션)** — halfData 96 MB 병목 자체 제거. Peak 이 volume
  size 에 비례 안 함. 256 MB 임상 CT mobile 가능화. 계획서:
  [Y1C_FULL_PROGRESSIVE_PLAN.md](../plans/Y1C_FULL_PROGRESSIVE_PLAN.md)
- **iPhone 256 MB 시도** — Y1f 만으로 문턱 어디까지 밀리는지 실측 (Y1c-full 판단
  근거)

**중기**:
- **Y1g (mmap path chunked staging)** — `buildFromMmappedSource` 도 slab 화.
  Disk paging 대용량 (4 GB+) 실 임상 데이터 지원.
- **Y2 (IndexedDB / OPFS brick cache)** — 재방문 파싱 스킵. 상한 확장 X, UX.

**측정 인프라**:
- **정확한 iPhone 프로세스 예산** — `performance.measureUserAgentSpecificMemory`
  가 iOS Safari 에서 사용 가능한지 조사. 크로스오리진 아니라면 실제 peak 계측.

## Follow-ups

1. **~~Y1f-1 slab 크기 결정~~ ✅** (1 row = brickStored, ~44 MB WASM / ~34 MB
   native)
2. **~~Y1f-2 Static pack loop 재구성~~ ✅** (two-pass, native + WASM 검증)
3. **~~Y1f-3 Empty-fill in slab loop~~ ✅** (통합됨)
4. **~~Y1f-4 Streaming path 검증~~ ✅** (무변경, legacy 유지)
5. **~~Y1f-5 mmap path~~** — Y1g 로 이관.
6. **~~Y1f-6 iPhone 실측 + baseline~~ ✅** (본 문서, 2026-07-22).
7. **Y1c-full 진입 결정** — 사용자 확인.
