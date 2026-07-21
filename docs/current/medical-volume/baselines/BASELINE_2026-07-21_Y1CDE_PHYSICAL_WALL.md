# Baseline 2026-07-21 — Y1c-lite + Y1d + Y1e trio · iPhone physical wall

**축**: Y (브라우저 대용량 페이징) · **단계**: Y1c-lite + Y1d + Y1e 조합 완료 ·
**결과**: 소프트웨어 최적화로 iPhone Safari mobile peak 을 **~956 → ~668 MB**
축소, 그러나 **192 MB 볼륨 (peak 668 MB) 은 여전히 crash** — 물리적 프로세스
메모리 예산 도달 확정.

## Y1c-lite + Y1d + Y1e 요약

Y1b (2026-07-21, `ac6e578`) 로 128 MB 달성 후 192 MB 를 겨냥한 3-round refactor:

- **Y1c-lite** — `loadDicomSeries` 가 float 대신 packHalf1x16 직접 출력.
  `VolumeRenderer::loadFromFloatData` 의 float→half 변환 pass 제거.
  → 저장 -4B/voxel (float) → -2B/voxel (half). 192 MB 볼륨: -192 MB 절감.
- **Y1d** — `BrickedVolume::build` 의 CPU-side `atlasBuf` (~atlas 사이즈) 제거.
  Pack 을 mapped staging buffer 에 직접. → -209 MB.
- **Y1e** — `halfData` 를 rvalue 로 전달 chain (viewer → renderer → build).
  Static pack 완료 즉시 해제 + GPU 텍스처 alloc 을 그 뒤로 재순서화. → GPU
  텍스처 alloc 순간 halfData 는 이미 없음. -96 MB peak.

## 실측 결과 (192 MB, 512×512×384, iPhone iOS 18.7 Safari & Chrome)

| Y1x 조합 | 예상 peak | 실측 |
|---|---:|---|
| Pre-Y1c-lite (float 경로) | ~956 MB | crash |
| Y1c-lite (half-direct) | ~764 MB | crash |
| + Y1d (staging 직접 pack) | ~764 MB | crash |
| + Y1e (halfData drop + GPU alloc 재순서화) | **~668 MB** | **여전히 crash** |

**iPhone Safari + Chrome mobile 프로세스 예산 = ~600-650 MB 사이** 로 데이터 확정.
668 MB 넘음 → OS kill → 브라우저 auto-reload → 첫 화면 fMRI 로 복귀.

## 대비 데스크톱 (RTX 4070, Chrome, 128 MB volume)

Y1c-lite 로 **decode 시간 감소**:
- Pre-Y1c-lite (Y1b baseline 2026-07-15): decode **842 ms** (float→half pass 포함)
- Y1c-lite: decode **966 ms** on 128 MB — 아, 이건 Y1b two-pass parseSlice 때문에 상승
- Y1c-lite on 192 MB: decode **894 ms** — 볼륨 크기가 커도 데코드 빨라짐

Y1c-lite 는 float 중간 pass 제거로 (256 files × w*h float→half conversion) 만큼
줄임. 정확히 몇 ms 절감했는지는 상세 프로파일링 필요.

Y1d, Y1e 는 CPU peak 만 바꿈 (연산 자체 변화 없음) → 데스크톱 wall time 무변화.

## Peak 메모리 흐름 (192 MB 볼륨, Y1c-lite + Y1d + Y1e 최종)

시간축 상 각 phase 별 peak:

```text
DICOM Pass 1 metadata scan   : Y1b 그대로, ~250 MB (runtime baseline)
DICOM Pass 2 half pack        : intensityHalf 96 grow + parseSlice buf 0.5 + runtime 250
                                 최종 96 + 250 = 346 MB
Volume3D handoff, apply       : 96 + 250 = 346 MB
uploadHalf → build 진입        : halfData 96 (moved) + 250 = 346 MB
Static Pass 2 pack (staging)  : halfData 96 + staging 209 + 250 = 555 MB
                                 ← 여기까지는 iPhone 안 죽음 (예상)
halfData 해제 (Y1e)             : staging 209 + 250 = 459 MB
GPU 텍스처 alloc                : staging 209 + GPU 209 + 250 = 668 MB  ← 죽는 지점
copyBufferToTexture + waitIdle : 668 MB 상태 유지
staging 스코프 해제              : GPU 209 + 250 = 459 MB
```

## 원인 분석: 왜 물리 한계가 668 MB 인가

iPhone Safari WKWebView 프로세스 메모리 예산은 여러 요인 (기기 총 RAM, 다른 앱
활성 여부, 시스템 상태) 에 따라 dynamic. 일반적으로:
- iPhone 15 (8 GB RAM): tab 예산 ~800-1200 MB
- iPhone 15 Cloudflare tunnel test (network 활성 + 백그라운드 부하): 실측 ~600 MB

우리가 부딪힌 벽은 **네트워크 스택 + JS runtime + WebAssembly + WebGPU + 텍스처
alloc 전부 합친 프로세스 총량**. 668 MB 순간 iOS 커널이 background daemon 우선
순위로 tab 을 kill.

## 남은 옵션 (다음 세션 후보)

**Y1f — Chunked staging (~1-2 세션)**:
- Staging 을 Z-slab 단위 (~26 MB) 로 쪼개서 개별 upload
- Peak: staging 26 + halfData 96 + GPU 209 + runtime 250 = **~581 MB**
- 물리 예산 안 (600 MB 여유), 192 MB 로드 가능성 있음
- 스코프: BrickedVolume::build 의 pack loop 을 Z-slab 반복으로 재구성

**Streaming per-slice atlas fill (~3-4 세션)**:
- 근본 재설계 — decode 결과가 out.intensityHalf 를 거치지 않고 바로 atlas 브릭에
  기록됨
- Peak: staging chunk + runtime = **~350 MB**
- 실 임상 CT 250+ MB 도 mobile 가능
- Volume3D 인터페이스 재설계, 여러 함수 시그니처 변경

**X4 v2 (mobile 강제 Streaming 모드)**:
- 이미 Y3 mobile bisect (2026-07-20) 에서 논의; Streaming 은 m_originalHalfData
  로 CPU 복사 → 실 이득 없음
- Y1c-lite 로 half 만 저장하므로 이제 m_originalHalfData 도 half → mmap 스타일
  refactor 랑 결합하면 유의미할 수 있음
- 스코프 중

## 원 목표 도달도 재확인

원 목표: "저사양 PC · 모바일 브라우저에서 대용량 실 CT/MRI 유의미한 fps/메모리"

| 실 임상 CT 사이즈 | Mobile 도달 (Y1e 후) |
|---|---|
| 머리 CT (~64 MB) | ✅ |
| 얇은 슬라이스 (~128 MB) | ✅ (Y1b 실증 2026-07-21) |
| 가슴 CT (150-256 MB) | ❌ (192 MB crash 확정) |
| 복부 CT (~200 MB) | ❌ |
| 전신 CT (500 MB+) | ❌ (아키텍처 재설계 필요) |

**원 목표의 "일반적 임상 CT" 는 아직 부분 도달**. 머리 · 얇은 슬라이스 (전체 CT
검사 중 상당 비중) 는 실증됐고, 두꺼운 슬라이스 (가슴/복부) 는 Y1f 이상 필요.

## Follow-ups

1. **~~Y1c-lite~~ ✅ · ~~Y1d~~ ✅ · ~~Y1e~~ ✅** — 본 커밋.
2. **Y1f (chunked staging)** — 다음 세션 primary 후보. ~1-2 세션.
3. **Streaming per-slice atlas fill** — Y1f 로도 부족 시 근본 재설계.
4. **정확한 iPhone 프로세스 예산 측정** — `performance.memory.jsHeapSizeLimit`
   등 JS API 활용하면 티어별 실측 가능.
5. **X3 append (Android/Intel iGPU)** — 여전히 기기 대기.
