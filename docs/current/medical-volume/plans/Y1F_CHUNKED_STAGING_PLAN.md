# Y1f — Chunked Staging (Z-slab granular upload)

**작성일**: 2026-07-22
**축**: Y (브라우저 대용량 페이징) · **단계**: Y1f
**전제**: Y1c-lite + Y1d + Y1e 완료 (`d924543`,
[BASELINE_2026-07-21_Y1CDE_PHYSICAL_WALL.md](../baselines/BASELINE_2026-07-21_Y1CDE_PHYSICAL_WALL.md))
**목표**: iPhone Safari 192 MB 볼륨 로드 성공. Y1e 시점 peak 668 MB 를
**~581 MB** 로 낮춰 iOS 프로세스 예산 (~600 MB) 안으로 진입.

---

## 1. 현재 정직한 메모리 회계 (Y1e 이후, 192 MB 볼륨)

Static path, 512×512×384 CT, Y1e 완료 시점:

| 시점 | 살아있는 것 | 합계 |
| --- | --- | --- |
| DICOM Pass 2 half pack | intensityHalf 96 + runtime 250 | 346 MB |
| build() 진입 (halfData moved) | halfData 96 + runtime 250 | 346 MB |
| Static pack (staging alloc) | halfData 96 + staging **209** + runtime 250 | 555 MB |
| halfData 해제 (Y1e) | staging **209** + runtime 250 | 459 MB |
| GPU 텍스처 alloc | staging **209** + GPU 209 + runtime 250 | **668 MB** ← 죽는 지점 |
| copyBufferToTexture + waitIdle | 668 유지 | 668 MB |
| staging 스코프 해제 | GPU 209 + runtime 250 | 459 MB |

**병목**: staging 209 MB 단일 할당. GPU 텍스처 209 MB 는 이미 최종 목표 사이즈
라 축소 불가 → **staging 만 축소** 해야 함.

---

## 2. 설계 — Z-slab 단위 staging + copy

`BrickedVolume::build` 의 Static Pass 2 pack 을 재구성:

**현재 (Y1e)**:
```
staging = alloc(atlasBytes)        // ~209 MB
mapped  = map(staging)
fillEmpty(mapped, atlas 전체)
for each brick:
    packBrickToStaging(brick, mapped, brickOffset)
unmap(staging)
copyBufferToTexture(staging → atlas, dstOffset = 0)
queue->waitIdle()
free(staging)
```

**Y1f**:
```
slabRows      = 1  (또는 2, 결정은 Y1f-1)
slabDepthVox  = slabRows * brickStored     // 예: 1 * 68 = 68
slabAtlasZ    = slabRows                    // 슬랩당 브릭 행 수
slabBytes     = atlasVoxelsL0.x * atlasVoxelsL0.y * slabDepthVox * bytesPerVoxel

staging = alloc(slabBytes)                  // ~26 MB (atlasGrid.z=8 기준)
for slabIdx in [0, atlasGrid.z / slabRows):
    mapped = map(staging)
    fillEmpty(mapped, slabDepthVox 만큼)
    for each brick with (atlasZ / slabRows) == slabIdx:
        localZ = brick.atlasZ - slabIdx * slabRows
        packBrickToStaging(brick, mapped, localOffsetFor(brick.x, brick.y, localZ))
    unmap(staging)
    copyBufferToTexture(staging → atlas,
                        dstOffset = (0, 0, slabIdx * slabDepthVox),
                        extent = (atlasVoxelsL0.x, atlasVoxelsL0.y, slabDepthVox))
    queue->waitIdle()
free(staging)
```

**핵심 결과**:
- Staging peak 209 → **~26 MB**
- GPU alloc 순간 staging 이 이미 slab 크기 → peak = 26 + 209 + 250 = **~485 MB**
- 여유 100+ MB → 256 MB 볼륨도 가능성 있음

## 3. Y1f-1: Slab 크기 결정 데이터

192 MB 케이스 atlasVoxelsL0 대략:
- pageGrid ≈ ceil(512/64) × ceil(512/64) × ceil(384/64) = 8×8×6 = 384 pages
- atlasGrid 계산 정확히는 build() 안의 자동 산정 로직 참조
- brickStored = 66 (interior 64 + halo 2)

Slab 크기 후보:
- **1 row** (Z=66 vox): staging ~ 8×66×8×66×66×2 ≈ **~46 MB**. 6회 반복.
- **2 rows** (Z=132 vox): ~92 MB. 3회 반복.
- **4 rows** (Z=264 vox): ~184 MB. 2회 반복.

**추천**: 1 row. 최대 절감. 6-7 회 `copyBufferToTexture` 는 감내.

(정확한 사이즈는 실제 atlasGrid 로그로 재확인. atlasVoxelsL0.x/y 는 halo 포함
이라 6×6 grid 기준 x·y 축 각 396~ vox → slab = 396×396×66×2 = ~20 MB.)

## 4. Atomic 단계 (총 ~1-2 세션)

### Y1f-2 — Static path pack loop 재구성 (~2-3h)

- `BrickedVolume::build()` Static branch 안의 pack 루프를 Z-slab 반복으로 재구성
- Slab staging 은 loop 밖에서 한 번 alloc, 매 iteration 에서 map/unmap
- 각 slab 안에서:
  - `fillEmpty(mapped, slabDepthVox)` — 그 slab 범위만
  - 브릭 순회 시 `atlasZ / slabRows == slabIdx` 필터
  - `packBrickToStaging` 의 destination offset 계산이 slab local Z 로
- `copyBufferToTexture` 호출 시 `dstOffset.z = slabIdx * slabDepthVox`,
  `extent.z = slabDepthVox`
- `queue->waitIdle()` 매 slab 뒤

### Y1f-3 — Empty-fill 통합 (~0.5h)

- 현재 코드에 이미 pack 전 fillEmpty 가 있으면 slab 안으로 이동
- 별도 atlas 전체 fill pass 있으면 제거
- Y1d 에서 이미 mapped staging 에 직접 fill 하도록 되어있음 → 그 로직을 slab 크기로만 조정

### Y1f-4 — Streaming path 검증 (~0.5h)

- Streaming 은 이미 per-brick upload (매 프레임 K=64) → 초기 대용량 staging 없음
- **하지만** 초기 4 LOD atlas empty-fill 시점에 큰 임시 buffer 를 만들지 않는지
  확인. `Streaming` mode 진입 시 build() 내 atlas alloc 경로가 empty fill 을
  어떻게 하는지 재점검
- 필요하면 여기도 slab 단위로

### Y1f-5 — `buildFromMmappedSource` path 동일 treatment (~1h)

- Disk paging (Step 5) 경로도 같은 pack 구조 → 같은 slab 리팩터
- Native regression 방지: RTX 4070 + 1024³ dense mmap 케이스 wall-time 확인

### Y1f-6 — 측정 baseline (~1-2h)

- **Primary**: iPhone Safari 192 MB 로드 성공 여부
- **Secondary**: iPhone Safari 256 MB 시도 (여유 데이터)
- **Regression**: RTX 4070 native + WASM 128/192 MB peak MB + total load time
- `BASELINE_2026-07-22_Y1F_CHUNKED_STAGING.md` 작성:
  - Peak MB before/after
  - Load time delta (slab 마다 waitIdle 로 100-200 ms 추가 예상)
  - iPhone Safari 성공 스크린샷 + FPS
  - Native 무회귀 확인
  - 실패 시 slab 크기 조정 실험 결과

### 합계: ~5-8시간 (1-2 세션)

---

## 5. 영향 받는 파일

| 단계 | 파일 |
| --- | --- |
| Y1f-2 | `src/rendering/BrickedVolume.cpp` (build Static branch) |
| Y1f-3 | `src/rendering/BrickedVolume.cpp` (fillEmpty 위치) |
| Y1f-4 | `src/rendering/BrickedVolume.cpp` (Streaming 초기 fill 재점검) |
| Y1f-5 | `src/rendering/BrickedVolume.cpp` (buildFromMmappedSource) |
| Y1f-6 | `docs/current/medical-volume/baselines/BASELINE_2026-07-22_Y1F_CHUNKED_STAGING.md` |

시그니처 변경 없음 → header 파일 무변경. 셰이더 무변경. 다른 컴포넌트 무영향.

---

## 6. 위험과 대응

| 위험 | 대응 |
| --- | --- |
| iPhone driver 가 다수 `copyBufferToTexture` 를 poorly handle | Slab 크기를 2/4 rows 로 늘려 반복 횟수 감소. Chunk 크기와 mobile 성공률 실측 매트릭스로 판단 |
| Slab 마다 waitIdle → 총 load-time 100-200 ms 증가 | 수용. Y1c-lite 로 decode 시간 이미 줄어 총 load-time 은 여전히 개선 방향 |
| `atlasGrid.z == 1` (단일-slab 극단) | Slab loop 이 1회 반복 = 기존 Y1e 와 동일. 무해 |
| Slab 경계에서 브릭 halo 가 겹침 | Halo 는 브릭 내부에 저장되므로 slab 경계와 무관. 별도 처리 없음 |
| 192 MB 도 여전히 crash (다른 요인) | 계획 그대로 baseline 에 기록, Y1c-full 로 진행. Y1f 자체는 여전히 peak -87 MB 성과 |
| Native regression (slab loop overhead) | `waitIdle` 6-7회는 native ~수 ms. RTX 4070 wall-time 무영향 예상 |

---

## 7. 성공 기준

| 지표 | 목표 |
| --- | --- |
| iPhone Safari 192 MB 로드 | 성공 (crash 없음, atlas 표시) |
| Peak MB (192 MB 볼륨, WASM) | 668 → **~485 MB** |
| Native 128/192 MB total load time | Y1e 대비 ±10% 이내 |
| Native 128/192 MB peak MB | Y1e 대비 감소 또는 동일 |
| Streaming 모드 초기 fill peak | 증가 없음 |

---

## 8. 후속 (이 계획 밖)

- **Y1c-full** — Y1f 로 192 MB 뚫으면 다음. Intensity 버퍼 자체 제거 →
  256 MB+ 임상 CT 가능화. [Y1C_FULL_PROGRESSIVE_PLAN.md](Y1C_FULL_PROGRESSIVE_PLAN.md)
- **Y2** — IndexedDB / OPFS 로 decoded brick cache. 상한 확장이 아닌 UX.

---

## 9. 다음 진입점

Y1f-1 (slab 크기 결정) 은 코드가 아니라 데이터 수집 — 다음 세션 시작 시
`BrickedVolume::build()` 초입 로그로 192 MB 케이스 실제 `atlasVoxelsL0` 를
찍은 뒤 slab bytes 계산. 그 결과로 slab rows 확정 후 Y1f-2 진입.
